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

#ifndef BITCOINRPC_H_
#define BITCOINRPC_H_

#include "Common.h"
#include "NetIO.h"
#include "utilities_js.hpp"

// thread safe
class BitcoinRpc {
public:
  string addr;
  SockAddr saddr;
  string user;
  string password;
  bool usessl;
public:
  BitcoinRpc(const string & addr, const string & user, const string& password,
      bool usessl = false);
  BitcoinRpc(const string & uriStr);
  ~BitcoinRpc();

  int jsonCall(const string& request, string& response, int64 timeoutMs);

  // 常见RPC函数
  bool CheckBitcoind();
};


inline bool CheckBitcoind(const string &bitcoindUri) {
  BitcoinRpc bitcoind(bitcoindUri);
  return bitcoind.CheckBitcoind();
}
#endif /* BITCOINRPC_H_ */
