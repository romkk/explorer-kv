/**
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef NETIO_H_
#define NETIO_H_

#include <sys/poll.h>
#include <deque>
#include "Common.h"
#include "Buffer.h"

enum SocketType {
  SOCKET_TCP = 0,
  SOCKET_UNIXDOMAIN = 1
};

/**
 * IPv4 socket address
 */
class SockAddr {
public:
  string ip;
  int port;
  uint32 ipInt;

  SockAddr() : ip("0.0.0.0"), port(0), ipInt(0) {}
  SockAddr(const string & ip, const int port) : ip(ip), port(port), ipInt(0) {}
  SockAddr(const string & addr);
  SockAddr(int port) : ip("0.0.0.0"), port(port), ipInt(0) {}
  void parse(const string & addr);
  string toString() const;

  SockAddr & operator=(const SockAddr & rhs) {
    ip = rhs.ip;
    port = rhs.port;
    return *this;
  }

  bool operator==(const SockAddr & rhs) const {
    return (ip == rhs.ip) && (port == rhs.port);
  }

  bool operator<(const SockAddr & rhs) const {
    if (ip == rhs.ip) {
      return port < rhs.port;
    } else {
      return ip < rhs.ip;
    }
  }

  uint32 getIpInt();
};

/**
 * Auto-close managed file descriptor
 */
class AutoFD {
public:
  int fd;
  AutoFD() : fd(-1) {}
  AutoFD(int fd): fd(fd) {}
  ~AutoFD() {
    if (fd >= 0) {
      ::close(fd);
    }
  }
  int swap(int newfd) {
    int ret = fd;
    fd = newfd;
    return ret;
  }
  operator int() {
    return fd;
  }
};

/**
 * A set of utility functions to deal with sockets, using raw FDs
 * is preferred, rather than wrap them to class.
 */
class NetUtil {
public:
  /**
   * Create a TCP server on the specified port
   * @return server socket FD
   */
  static int server(int port);

  /**
   * Create a TCP server on the specified address(both ip and port)
   * if local.ip equals 0, this method will choose a available port
   * automatically and assign back to local.ip
   * @return server socket FD
   */
  static int server(SockAddr & local);

  /**
   * Server side accept a connection
   * @param sock sever socket FD
   * @param remote get remote side socket address when return
   * @return connection socket FD
   */
  static int accept(int sock, SockAddr & remote);

  /**
   * Get local address of a connected socket
   * @param sock
   * @param addr
   * @return
   */
  static bool getLocalAddr(int sock, SockAddr & addr);
  /**
   * Client side connect to server
   * TODO: support timeout
   * @param remote server address
   * @param nonBlock whether using nonBlocking connect
   * @return connection socket FD
   */
  static int connect(const SockAddr & remote, bool nonBlock = false);

  /**
   * Client side connect to server
   * @param remote server address
   * @param local get local side socket address when return
   * @nonBlock whether using nonBlocking connect
   * @return connection socket FD
   */
  static int connect(const SockAddr & remote, SockAddr & local, bool nonBlock = false);

  /**
   * Get IP from hostname
   * @return true if succeed
   */
  static bool resolve(const string & host, string & ip);

  // Set FD options
  static bool nonBlock(int fd, bool nonBlock=true);
  static void tcpNoDelay(int fd, bool noDelay);
  static void tcpKeepAlive(int fd, bool keepAlive);
  static void tcpSendBuffer(int fd, uint32 size);
  static void tcpRecvBuffer(int fd, uint32 size);
};

/**
 * IO events, borrow poll/epoll macros
 */
#ifdef __linux__
#include <sys/epoll.h>
const uint32_t EVENT_READ  = EPOLLIN;
const uint32_t EVENT_WRITE = EPOLLOUT;
const uint32_t EVENT_CLOSE = EPOLLHUP;
const uint32_t EVENT_ERROR = EPOLLERR;
const uint32_t EVENT_ALL   = EVENT_READ | EVENT_WRITE;
#else
const int16 EVENT_READ  = POLLIN | POLLPRI;
const int16 EVENT_WRITE = POLLOUT;
const int16 EVENT_CLOSE = POLLHUP;
const int16 EVENT_ERROR = POLLERR;
const int16 EVENT_ALL   = EVENT_READ | EVENT_WRITE;
#endif

string EventToString(uint32_t event);


class Session {
public:
  int fd;
  uint32_t eventsSubscribed;
  uint32_t events;
  int64 timeout;

  Session() : fd(-1), eventsSubscribed(0), events(0), timeout(Time::MAX) {}
  Session(int fd) : fd(fd), eventsSubscribed(0), events(0), timeout(Time::MAX) {}
  ~Session() {
    if (fd > 0) {
      ::close(fd);
      fd = -1;
    }
  }
  int getFd() {
    return fd;
  }
  void setFd(int fd) {
    this->fd = fd;
  }

  bool operator<(const Session & rhs) const {
    return timeout < rhs.timeout;
  }
};
typedef Session* SessionPtr;


/**
 * Poll based poller
 * not so efficient as epoll, but it is available in macosx
 * for testing
 */
class PollPoller {
public:
  PollPoller();
  ~PollPoller();
  void update(Session * session, int16 events);
  void remove(Session * session);
  int poll(int32 timeoutMs, vector<SessionPtr> & sessions);
  void wakeup();
private:
  int wakefd_[2];
  vector<pollfd> polls_;
  typedef map<int, std::pair<int, SessionPtr>> FdSessionMap;
  FdSessionMap fdSessionMap_;
};


#ifdef __linux__

#include <sys/epoll.h>

/**
 * Epoll based poller
 */
#define MAXEVENTS 2048
class EPollPoller {
public:
  EPollPoller();
  ~EPollPoller();
  void update(Session * session, uint32_t eventsSubscribed);
  void remove(Session * session);
  int poll(int32 timeoutMs, vector<SessionPtr> & sessions);
  void wakeup();
private:
  int _wakefd[2];
  int _epollfd;
  struct epoll_event _tempEvent;
  struct epoll_event *_events;
  std::unordered_set<SessionPtr> _sessions;
};

typedef EPollPoller Poller;

#else

typedef PollPoller Poller;

#endif


#endif /* NETIO_H_ */
