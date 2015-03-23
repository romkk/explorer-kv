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

#ifndef URI_H_
#define URI_H_

#include <string>

using std::string;

/**
 * URI class with parser, support components include scheme,
 * authority(userInfo, host, port), path, query and fragment.
 *
 * Only some predefined schemes are supported:
 *  http|https|ftp|file|bitcoind|mysql
 *
 * The implementation is simplified to only support minimum required
 * features, don't expect it to be fully compatible with standard.
 *
 * Escape not supported
 * If there is ambiguity in host or path, for example:
 *   hadoop.apache.com  or hello
 * Could both mean a valid path or a valid host, we prefer host
 */
class URI {
private:
  string _uri;
  string _scheme;
  string _authority;
  string _userInfo;
  string _host;
  int    _port;
  string _path;
  string _query;
  string _fragment;

public:
  /**
   * Default constructor
   */
  URI();

  /**
   * Construct normal service URI, such as HDFS namenode service
   */
  URI(const string & scheme, const string & host, int port);

  /**
   * Construct URI with path, such as HDFS path
   */
  URI(const string & scheme, const string & host, int port, const string & path);

  ~URI() {}

  /**
   * Parse URI from a URI string
   */
  bool parse(const string & uri);

  /**
   * Generate URI string from its sub-components
   */
  const string & toString();

  /**
   * Generate description of this URI
   */
  string getDescription() const;

  /**
   * Generate authority from sub-components(userInfo, host, port)
   */
  const string & toAuthority();

  /**
   * Get user from userInfo
   */
  string getUser() const;

  /**
   * Get password from userInfo
   */
  string getPassword() const;

  // Getters and Setters

  const string & getAuthority() const {
    return _authority;
  }

  const string & getFragment() const {
    return _fragment;
  }

  void setFragment(const string & fragment) {
    _fragment = fragment;
    toString();
  }

  const string & getHost() const {
    return _host;
  }

  void setHost(const string & host) {
    _host = host;
    toString();
  }

  const string & getPath() const {
    return _path;
  }

  void setPath(const string & path) {
    _path = path;
    toString();
  }

  int getPort() const {
    return _port;
  }

  void setPort(int port) {
    _port = port;
    toString();
  }

  const string & getQuery() const {
    return _query;
  }

  void setQuery(const string & query) {
    _query = query;
    toString();
  }

  const string & getScheme() const {
    return _scheme;
  }

  void setScheme(const string & scheme) {
    _scheme = scheme;
    toString();
  }

  const string & getUri() const {
    return _uri;
  }

  void setUri(const string & uri) {
    parse(uri);
  }

  const string & getUserInfo() const {
    return _userInfo;
  }

  void setUserInfo(const string & userInfo) {
    _userInfo = userInfo;
    toString();
  }

  void setUserInfo(const string & user, const string & password);

  bool sameService(const URI & rhs) const;
};

#endif /* URI_H_ */
