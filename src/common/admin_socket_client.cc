// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2011 New Dream Network
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 *
 */

#include "common/admin_socket.h"
#include "common/ceph_context.h"
#include "common/errno.h"
#include "common/safe_io.h"
#include "common/admin_socket_client.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <map>
#include <poll.h>
#include <sstream>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
#include <vector>

using std::ostringstream;

static std::string asok_connect(const std::string &path, int *fd)
{
  int socket_fd = socket(PF_UNIX, SOCK_STREAM, 0);
  if(socket_fd < 0) {
    int err = errno;
    ostringstream oss;
    oss << "socket(PF_UNIX, SOCK_STREAM, 0) failed: " << cpp_strerror(err);
    return oss.str();
  }

  struct sockaddr_un address;
  memset(&address, 0, sizeof(struct sockaddr_un));
  address.sun_family = AF_UNIX;
  snprintf(address.sun_path, sizeof(address.sun_path), "%s", path.c_str());

  if (::connect(socket_fd, (struct sockaddr *) &address, 
	sizeof(struct sockaddr_un)) != 0) {
    int err = errno;
    ostringstream oss;
    oss << "connect(" << socket_fd << ") failed: " << cpp_strerror(err);
    close(socket_fd);
    return oss.str();
  }

  struct timeval timer;
  timer.tv_sec = 5;
  timer.tv_usec = 0;
  if (::setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timer, sizeof(timer))) {
    int err = errno;
    ostringstream oss;
    oss << "setsockopt(" << socket_fd << ", SO_RCVTIMEO) failed: "
	<< cpp_strerror(err);
    close(socket_fd);
    return oss.str();
  }
  timer.tv_sec = 5;
  timer.tv_usec = 0;
  if (::setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO, &timer, sizeof(timer))) {
    int err = errno;
    ostringstream oss;
    oss << "setsockopt(" << socket_fd << ", SO_SNDTIMEO) failed: "
	<< cpp_strerror(err);
    close(socket_fd);
    return oss.str();
  }

  *fd = socket_fd;
  return "";
}

static std::string asok_request(int socket_fd, uint32_t request_id)
{
  uint32_t request_id_raw = htonl(0x1);
  ssize_t res = safe_write(socket_fd, &request_id_raw, sizeof(request_id_raw));
  if (res < 0) {
    int err = res;
    ostringstream oss;
    oss << "safe_write(" << socket_fd << ") failed to write request code: "
	<< cpp_strerror(err);
    return oss.str();
  }
  return "";
}

AdminSocketClient::
AdminSocketClient(const std::string &path)
  : m_path(path)
{
}

std::string AdminSocketClient::
send_noop()
{
  int socket_fd;
  std::string err = asok_connect(m_path, &socket_fd);
  if (!err.empty()) {
    goto done;
  }
  err = asok_request(socket_fd, 0x0);
  if (!err.empty()) {
    goto done;
  }
done:
  close(socket_fd);
  return err;
}

std::string AdminSocketClient::
get_message(std::string *message)
{
  int socket_fd, res;
  std::vector<uint8_t> vec(65536, 0);
  uint8_t *buffer = &vec[0];
  uint32_t message_size_raw, message_size;

  std::string err = asok_connect(m_path, &socket_fd);
  if (!err.empty()) {
    goto done;
  }
  err = asok_request(socket_fd, 0x1);
  if (!err.empty()) {
    goto done;
  }
  res = safe_read_exact(socket_fd, &message_size_raw,
				sizeof(message_size_raw));
  if (res < 0) {
    int e = res;
    ostringstream oss;
    oss << "safe_read(" << socket_fd << ") failed to read message size: "
	<< cpp_strerror(e);
    err = oss.str();
    goto done;
  }
  message_size = ntohl(message_size_raw);
  res = safe_read_exact(socket_fd, buffer, message_size);
  if (res < 0) {
    int e = res;
    ostringstream oss;
    oss << "safe_read(" << socket_fd << ") failed: " << cpp_strerror(e);
    err = oss.str();
    goto done;
  }
  //printf("MESSAGE FROM SERVER: %s\n", buffer);
  message->assign((const char*)buffer);
done:
  close(socket_fd);
  return err;
}
