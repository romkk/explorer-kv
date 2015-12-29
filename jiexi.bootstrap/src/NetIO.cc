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

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <algorithm>
#include "NetIO.h"

SockAddr::SockAddr(const string & addr) : port(0), ipInt(0) {
  parse(addr);
}

void SockAddr::parse(const string & addr) {
  size_t pos = addr.rfind(':');
  if (pos == string::npos) {
    ip.clear();
    port = 0;
    return;
  }
  ip = addr.substr(0, pos);
  port = Strings::toInt(addr.substr(pos + 1));
}

string SockAddr::toString() const {
  if (port == 0 || ip.empty()) {
    return "";
  }
  else {
    return Strings::Format("%s:%d", ip.c_str(), port);
  }
}

uint32 SockAddr::getIpInt() {
  if (ipInt == 0 && !ip.empty()) {
    ipInt = Str2Ip(ip.c_str());
  }
  return ipInt;
}

int NetUtil::server(int port) {
  SockAddr addr("0.0.0.0", port);
  int ret = server(addr);
  if (port == 0) {
    port = addr.port;
  }
  return ret;
}

int NetUtil::server(SockAddr & local) {
  int ret = 0;
  if ((ret = ::socket(AF_INET, SOCK_STREAM, 0)) == -1) {
    THROW_EXCEPTION_EX(ERROR_UNKNOWN, "creating socket error: %s",
                       strerror(errno));
  }
#ifdef SO_REUSEADDR
  // set SO_REUSEADDR on a socket to true (1):
  int optval = 1;
  setsockopt(ret, SOL_SOCKET, SO_REUSEADDR, (const char *)&optval, sizeof(optval));
#endif
  struct sockaddr_in sa;
  memset(&sa, 0, sizeof(sa));
  sa.sin_family = AF_INET;
  sa.sin_port = htons(local.port);
  sa.sin_addr.s_addr = htonl(INADDR_ANY);
  if (!local.ip.empty()) {
    if (inet_pton(AF_INET, local.ip.c_str(), &sa.sin_addr) == 0) {
      close(ret);
      THROW_EXCEPTION_EX(ERROR_UNKNOWN, "inet_aton(%s) error", local.ip.c_str());
    }
  }
  if (::bind(ret, (sockaddr*) &sa, sizeof(sa)) == -1) {
    close(ret);
    THROW_EXCEPTION_EX(ERROR_UNKNOWN, "bind(%s) to socket %d failed: %s",
                       local.toString().c_str(), ret, strerror(errno));
  }
  if (::listen(ret, 1023) == -1) {
    close(ret);
    THROW_EXCEPTION_EX(ERROR_UNKNOWN, "listen(%d, 1023) error: %s",
                       ret, strerror(errno));
  }
  if (sa.sin_port == 0) {
    // get real port
    socklen_t slen = sizeof(sa);
    ::getsockname(ret, (sockaddr*)&sa, &slen);
    local.port = ntohs(sa.sin_port);
  }
  return ret;
}

int NetUtil::accept(int sock, SockAddr & remote) {
  struct sockaddr_in sa;
  memset(&sa, 0, sizeof(sa));
  socklen_t salen = sizeof(sa);
  int ret = 0;
  while (true) {
    ret = ::accept(sock, (sockaddr*) &sa, &salen);
    if (ret == -1) {
      if (errno == EINTR || errno == EWOULDBLOCK) {
        return -1;
      } else {
        THROW_EXCEPTION_EX(errno, "accept error %s", strerror(errno));
      }
    } else {
      break;
    }
  }
  remote.port  = ntohs(sa.sin_port);
  remote.ipInt = ntohl(*(uint32_t *)&(sa.sin_addr.s_addr));
  remote.ip    = Ip2StrNetOrder(sa.sin_addr.s_addr);
  return ret;
}

int NetUtil::connect(const SockAddr & remote, bool nonBlock) {
  SockAddr local;
  return connect(remote, local, nonBlock);
}

int NetUtil::connect(const SockAddr & remote, SockAddr & local, bool nonBlock) {
  if (remote.ip.empty() || remote.port == 0) {
    THROW_EXCEPTION_EX(EINVAL, "connect error: empty sever address");
  }
  int ret = 0;
  // create socket
  if ((ret = ::socket(AF_INET, SOCK_STREAM, 0)) == -1) {
    THROW_EXCEPTION_EX(ERROR_UNKNOWN, "creating socket error: %s",
        hstrerror(h_errno));
  }
  // set nonblocking
  if (nonBlock) {
    if (!NetUtil::nonBlock(ret)) {
      close(ret);
      THROW_EXCEPTION_EX(ERROR_UNKNOWN,
          "can't use nonblocking connect for socket %d, close", ret);
    }
  }
  // get remote sockaddr
  struct sockaddr_in sa;
  memset(&sa, 0, sizeof(sa));
  sa.sin_family = AF_INET;
  sa.sin_port = htons(remote.port);
  if (inet_pton(AF_INET, remote.ip.c_str(), &sa.sin_addr) == 0) {
    struct hostent *he;
    he = ::gethostbyname(remote.ip.c_str());
    if (he == nullptr) {
      close(ret);
      THROW_EXCEPTION_EX(ERROR_UNKNOWN, "can't resolve: %s", remote.ip.c_str());
    }
    memcpy(&sa.sin_addr, he->h_addr, sizeof(struct in_addr));
  }
  // connect
  if (::connect(ret, (struct sockaddr*) &sa, sizeof(sa)) == -1) {
    if ((errno == EINPROGRESS) && nonBlock) {
      return ret;
    }
    close(ret);
    THROW_EXCEPTION_EX(ERROR_UNKNOWN, "connect to %s error: %s",
                       remote.toString().c_str(), hstrerror(h_errno));
  }
  if (!getLocalAddr(ret, local)) {
    LOG_DEBUG("Get local address of socket(fd=%d) failed", ret);
  }
  return ret;
}

bool NetUtil::getLocalAddr(int sock, SockAddr & addr) {
  struct sockaddr_in saLocal;
  memset(&saLocal, 0, sizeof(saLocal));
  socklen_t saLocalLen = sizeof(saLocal);
  if (::getsockname(sock, (sockaddr*)&saLocal, &saLocalLen) == 0) {
    addr.ip   = Ip2StrNetOrder(saLocal.sin_addr.s_addr);
    addr.port = ntohs(saLocal.sin_port);
    return true;
  } else {
    addr.ip.clear();
    addr.port = 0;
    return false;
  }
}

bool NetUtil::nonBlock(int fd, bool nonBlock) {
  int flags;
  if ((flags = fcntl(fd, F_GETFL)) == -1) {
    LOG_FATAL("Error fcntl(F_GETFL): %s", strerror(errno));
    return false;
  }
  if (nonBlock) {
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
      LOG_FATAL("Error set O_NONBLOCK: %s", strerror(errno));
      return false;
    }
  } else {
    if (fcntl(fd, F_SETFL, flags & (~O_NONBLOCK)) == -1) {
      LOG_FATAL("Error unset O_NONBLOCK: %s", strerror(errno));
      return false;
    }
  }
  return true;
}

void NetUtil::tcpNoDelay(int fd, bool noDelay) {
  int op = noDelay ? 1 : 0;
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &op, sizeof(op));
}

void NetUtil::tcpKeepAlive(int fd, bool keepAlive) {
  int op = keepAlive ? 1 : 0;
  setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &op, sizeof(op));
}

void NetUtil::tcpSendBuffer(int fd, uint32 size) {
  setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size));
}

void NetUtil::tcpRecvBuffer(int fd, uint32 size) {
  setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size));
}

bool NetUtil::resolve(const string & host, string & ip) {
  struct sockaddr_in sa;
  memset(&sa, 0, sizeof(sa));
  if (inet_aton(host.c_str(), &sa.sin_addr) == 0) {
    struct hostent *he;
    he = ::gethostbyname(host.c_str());
    if (he == nullptr) {
      return false;
    }
    memcpy(&sa.sin_addr, he->h_addr, sizeof(struct in_addr));
    ip = inet_ntoa(sa.sin_addr);
  }
  else {
    ip = host;
  }
  return true;
}


string EventToString(uint32_t event) {
  string ret;
  if (event & EVENT_READ) {
    ret.append("read");
  }
  if (event & EVENT_WRITE) {
    if (!ret.empty()) {
      ret.append(",");
    }
    ret.append("write");
  }
  if (event & EVENT_CLOSE) {
    if (!ret.empty()) {
      ret.append(",");
    }
    ret.append("close");
  }
  if (event & EVENT_ERROR) {
    if (!ret.empty()) {
      ret.append(",");
    }
    ret.append("error");
  }
  return ret;
}


PollPoller::PollPoller() {
  polls_.reserve(4000);
  if (pipe(wakefd_) != 0) {
    wakefd_[0] = -1;
    wakefd_[1] = -1;
    throw std::runtime_error(strerror(errno));
  }
  NetUtil::nonBlock(wakefd_[0]);
  NetUtil::nonBlock(wakefd_[1]);
  polls_.push_back(pollfd());
  polls_[0].fd = wakefd_[0];
  polls_[0].events = EVENT_READ;
  polls_[0].revents = 0;
}

PollPoller::~PollPoller() {
  if (wakefd_[0] >= 0) {
    close(wakefd_[0]);
    wakefd_[0] = -1;
  }
  if (wakefd_[1] >= 0) {
    close(wakefd_[1]);
    wakefd_[1] = -1;
  }
}

void PollPoller::update(Session * session, int16 events) {
  session->eventsSubscribed = events;
  int fd = session->getFd();
  auto itr = fdSessionMap_.find(fd);
  if (itr != fdSessionMap_.end()) {
    pollfd & p = polls_[itr->second.first];
    assert(itr->second.second == session);
    p.events = session->eventsSubscribed;
  } else {
    int pos = (int)polls_.size();
    polls_.push_back(pollfd());
    polls_[pos].fd = fd;
    polls_[pos].events = session->eventsSubscribed;
    polls_[pos].revents = 0;
    fdSessionMap_[fd] = std::make_pair(pos, session);
  }
}

void PollPoller::remove(Session * session) {
  auto itr = fdSessionMap_.find(session->fd);
  if (itr != fdSessionMap_.end()) {
    fdSessionMap_.erase(itr);
    int index = itr->second.first;
    // indexes needs to be updated when some session are deleted
    assert((index != 0));
    assert(index >= polls_.size());
    if (index == polls_.size() - 1) {
      // at last, just remove
      polls_.pop_back();
    } else {
      // move last to this empty slot
      polls_[index] = polls_[polls_.size() - 1];
      polls_.pop_back();
      // update index
      auto update = fdSessionMap_.find(polls_[index].fd);
      if (update != fdSessionMap_.end()) {
        update->second.first = index;
      } else {
        assert(false);
      }
    }
  } else {
    assert(false);
  }
}

int PollPoller::poll(int timeoutMs, vector<SessionPtr> & sessions) {
  int ret = ::poll(&polls_[0], polls_.size(), timeoutMs);
  if (ret < 0) {
    // error
    if (errno == EAGAIN || errno == EINTR) {
      return 0;
    } else {
      THROW_EXCEPTION_EX(errno, "poll failed: %s", strerror(errno));
    }
  } else if (ret == 0) {
    // timeout
    return 0;
  }
  if (polls_[0].revents & EVENT_READ) {
    char buff[8];
    while (::read(polls_[0].fd, buff, 8) > 0);
  }
  ret = 0;
  for (int i = 1; i < polls_.size(); i++) {
    pollfd & pf = polls_[i];
    if (pf.revents) {
      auto itr = fdSessionMap_.find(pf.fd);
      assert(itr != fdSessionMap_.end());
      assert(i == itr->second.first);
      Session * s = itr->second.second;
      assert(s->fd == pf.fd);
      s->events = pf.revents;
      sessions.push_back(s);
      pf.revents = 0;
      ret++;
    }
  }
  return ret;
}

void PollPoller::wakeup() {
  int64_t event = 1;
  ssize_t wt = ::write(wakefd_[1], (void*)&event, sizeof(event));
  if (wt != sizeof(event)) {
    LOG_WARN("wakeup write pipe(%d) failed, wt=%lld", wakefd_[1], wt);
  }
}

///////////////////////////////////////////////////////////

#ifdef __linux__

//
// The void *ptr and int fd both are inside a union inside the struct
// epoll_event. You should use either of them not both.
//
EPollPoller::EPollPoller() {
	_events = (struct epoll_event *)calloc(MAXEVENTS, sizeof(struct epoll_event));
	
  if (pipe(_wakefd) != 0) {
    _wakefd[0] = -1;
    _wakefd[1] = -1;
    throw std::runtime_error(strerror(errno));
  }
  NetUtil::nonBlock(_wakefd[0]);
  NetUtil::nonBlock(_wakefd[1]);
  _epollfd = ::epoll_create1(EPOLL_CLOEXEC);
  _tempEvent.data.ptr = nullptr;  // 将这个指针设置为零
  _tempEvent.events   = EPOLLIN | EPOLLET;
  if (::epoll_ctl(_epollfd, EPOLL_CTL_ADD, _wakefd[0], &_tempEvent) != 0) {
		LOG_WARN("epoll_ctl failed, err(%d): %s", errno, strerror(errno));
	}
}

EPollPoller::~EPollPoller() {
  if (_epollfd >= 0) {
    close(_epollfd);
    _epollfd = -1;
  }
  if (_wakefd[0] >= 0) {
    close(_wakefd[0]);
    _wakefd[0] = -1;
  }
  if (_wakefd[1] >= 0) {
    close(_wakefd[1]);
    _wakefd[1] = -1;
  }
}

void EPollPoller::update(Session * session, uint32_t eventsSubscribed) {
  session->eventsSubscribed = eventsSubscribed;
  int fd = session->fd;
  auto itr = _sessions.find(session);
  if (itr != _sessions.end()) {
    _tempEvent.data.ptr = session;
    _tempEvent.events   = eventsSubscribed;
    if (::epoll_ctl(_epollfd, EPOLL_CTL_MOD, fd, &_tempEvent) != 0) {
			LOG_WARN("epoll_ctl failed, err(%d): %s", errno, strerror(errno));
		}
  } else {
    _tempEvent.data.ptr = session;
    _tempEvent.events   = eventsSubscribed;
    if (::epoll_ctl(_epollfd, EPOLL_CTL_ADD, fd, &_tempEvent) != 0) {
			LOG_WARN("epoll_ctl failed, err(%d): %s", errno, strerror(errno));
		}
    _sessions.insert(session);
  }
}

void EPollPoller::remove(Session * session) {
  auto itr = _sessions.find(session);
  if (itr != _sessions.end()) {
		_tempEvent.data.ptr = session;
    _tempEvent.events   = 0;
    if (::epoll_ctl(_epollfd, EPOLL_CTL_DEL, session->fd, &_tempEvent) != 0) {
			LOG_WARN("epoll_ctl failed, err(%d): %s", errno, strerror(errno));
		}
    _sessions.erase(session);
  } else {
    assert(false);
  }
}

int EPollPoller::poll(int32 timeoutMs, vector<SessionPtr> & sessions) {
  int nfds = ::epoll_wait(_epollfd, _events, MAXEVENTS, timeoutMs);
  if (nfds < 0) {
    // error
    if (errno == EAGAIN || errno == EINTR) {
      return 0;
    } else {
      THROW_EXCEPTION_EX(errno, "epoll failed: %s", strerror(errno));
    }
  }
  else if (nfds == 0) {
    // timeout
    return 0;
  }
	
  int ret = 0;
  for (int i = 0; i < nfds; i++) {
    epoll_event &ev = _events[i];  // alias
    if (ev.data.ptr == nullptr) {
      if ((ev.events & EPOLLIN) == EPOLLIN) {
        char buff[8];
        while (::read(_wakefd[0], buff, sizeof(buff)) > 0);
      } else {
        THROW_EXCEPTION_EX(ERROR_UNKNOWN, "pipe fd(%p) in poller error", ev.data.ptr);
      }
    } else {
      Session *s = (Session *)(ev.data.ptr);
			if (s != nullptr) {
        s->events = ev.events;
        sessions.push_back(s);
				ret++;
			}
    }
  } /* /for */
  return ret;
}

void EPollPoller::wakeup() {
	char c = 1;
  ssize_t wt = ::write(_wakefd[1], (void*)&c, sizeof(c));
  if (wt != sizeof(c)) {
    LOG_WARN("wakeup write pipe(%d) failed, wt=%lld", _wakefd[1], wt);
  }
}

#endif

