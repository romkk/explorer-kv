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

#include <sstream>
#include <boost/regex.hpp>
#include "Common.h"
#include "URI.h"

URI::URI() : _port(0) {
}

URI::URI(const string & scheme, const string & host, int port) :
    _scheme(scheme),
    _host(host),
    _port(port) {
  toString();
}

URI::URI(const string & scheme, const string & host, int port,
         const string & path) :
    _scheme(scheme),
    _host(host),
    _port(port),
    _path(path) {
  toString();
}

static boost::regex UriRegex(
    "((http|https|ftp|file|bitcoind|mysql)://)?" // scheme
    "(([^@:]+(:[^@:]+)?@)?([a-zA-Z0-9_-][a-zA-Z0-9\\._-]*)(\\:(\\d+))?)?" // authority
    "([^#\\:\\?]*)(\\?([^#]*))?(#([^\\b#]*))?$" // path, query, fragment
);

bool URI::parse(const string & uri) {
  boost::smatch results;
  if (!boost::regex_match(uri, results, UriRegex)) {
    return false;
  }
  _scheme = results.str(2);
  _authority = results.str(3);
  _userInfo = results.str(4);
  if (!_userInfo.empty()) {
    // remove ending '@'
    _userInfo.resize(_userInfo.length() - 1);
  }
  _host = results.str(6);
  string portStr = results.str(8);
  _port = Strings::toInt(portStr);
  _path = results.str(9);
  _query = results.str(11);
  _fragment = results.str(13);
  toString();
  return true;
}


const string & URI::toString() {
  std::ostringstream os;
  if (!_scheme.empty()) {
    os << _scheme << "://";
  }
  if (!_userInfo.empty()) {
    os << _userInfo;
    if (!_host.empty()) {
      os << "@";
    }
  }
  if (!_host.empty()) {
    os << _host;
  }
  if (_port != 0) {
    os << ":" << _port;
  }
  if (!_path.empty()) {
    os << _path;
  }
  if (!_query.empty()) {
    os << "?" << _query;
  }
  if (!_fragment.empty()) {
    os << "#" << _fragment;
  }
  _uri = os.str();
  return getUri();
}

string URI::getDescription() const {
  return Strings::Format(
      "{scheme: %s, userInfo: %s, host: %s, port: %d, path: %s, query: %s, fragment: %s, uri: %s}",
      _scheme.c_str(), _userInfo.c_str(), _host.c_str(), _port, _path.c_str(),
      _query.c_str(), _fragment.c_str(), _uri.c_str());
}

const string & URI::toAuthority() {
  return getAuthority();
}

string URI::getUser() const {
  size_t pos = _userInfo.find(':');
  if (pos == string::npos) {
    return _userInfo;
  }
  return _userInfo.substr(0, pos);
}

string URI::getPassword() const {
  size_t pos = _userInfo.find(':');
  if (pos == string::npos) {
    return string();
  }
  return _userInfo.substr(pos+1);
}

void URI::setUserInfo(const string & user, const string & password) {
  setUserInfo(user + ":" + password);
}

bool URI::sameService(const URI & rhs) const {
  if (_scheme == rhs._scheme) {
    if (_scheme == "file") {
      return true;
    }
    return (_userInfo == rhs._userInfo) &&
           (_host == rhs._host) &&
           (_port == rhs._port);
  }
  return false;
}
