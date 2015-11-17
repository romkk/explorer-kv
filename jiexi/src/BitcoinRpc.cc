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

#include <boost/iostreams/concepts.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include "bitcoin/util.h"
#include "bitcoin/rpcprotocol.h"
#include "utilities_js.hpp"
#include "URI.h"
#include "BitcoinRpc.h"

using namespace std;
using namespace boost;
using namespace boost::asio;


////////////////////////////////  BitcoinRpc  //////////////////////////////////
BitcoinRpc::BitcoinRpc(const string& addr, const string& user,
    const string& password, bool usessl) :
    addr(addr), saddr(addr), user(user), password(password), usessl(usessl) {
}

BitcoinRpc::BitcoinRpc(const string & uriStr) {
  URI uri;
  if (!uri.parse(uriStr)) {
    THROW_EXCEPTION_EX(EINVAL, "create BitcoinRpc failed, illegal uri: %s", uriStr.c_str());
  }
  addr = Strings::Format("%s:%d", uri.getHost().c_str(), uri.getPort());
  saddr = SockAddr(addr);
  user = uri.getUser();
  password = uri.getPassword();
  usessl = false;
}

BitcoinRpc::~BitcoinRpc() {
}

// 该函数可以多线程调用
int BitcoinRpc::jsonCall(const string& request, string& response, int64 timeoutMs) {
  try {
    int64 expire = Time::Timeout(timeoutMs);
    asio::io_service io_service;
    ssl::context context(io_service, ssl::context::sslv23);
    context.set_options(ssl::context::no_sslv2);
    asio::ssl::stream<asio::ip::tcp::socket> sslStream(io_service, context);
    SSLIOStreamDevice<asio::ip::tcp> d(sslStream, usessl);
    iostreams::stream< SSLIOStreamDevice<asio::ip::tcp> > stream(d);

    do {
        bool fConnected = d.connect(saddr.ip, Strings::ToString(saddr.port));
        if (fConnected) break;
        int64 now = Time::CurrentMonoMill();
        if (now < expire) {
          LOG_WARN("Connect to %s failed, sleep 1s and retry", addr.c_str());
          Thread::Sleep(1000);
        } else {
          LOG_WARN("Connect to %s timeout, abort", addr.c_str());
          return 1;
        }
    } while (true);

    // HTTP basic authentication
    string strUserPass64 = EncodeBase64(user + ":" + password);
    map<string, string> mapRequestHeaders;
    mapRequestHeaders["Authorization"] = string("Basic ") + strUserPass64;

    string strPost = HTTPPost(request, mapRequestHeaders);
    stream << strPost << std::flush;

    // Receive HTTP reply status
    int nProto = 0;
    int nStatus = ReadHTTPStatus(stream, nProto);

    // Receive HTTP reply message headers and body
    map<string, string> mapHeaders;
    ReadHTTPMessage(stream, mapHeaders, response, nProto);

    if (nStatus == HTTP_UNAUTHORIZED) {
      LOG_WARN("incorrect rpcuser or rpcpassword (authorization failed)");
      return 2;
    }
    else if (nStatus >= 400 && nStatus != HTTP_BAD_REQUEST
        && nStatus != HTTP_NOT_FOUND && nStatus != HTTP_INTERNAL_SERVER_ERROR) {
      LOG_WARN("server returned HTTP error %d", nStatus);
      return 2;
    }
    else if (response.empty()) {
      LOG_WARN("no response from server");
      return 2;
    }
//    LOG_DEBUG("[BitcoinRpc::jsonCall] response: %s", response.c_str());
    return 0;
  }
  catch (std::exception& e)
  {
    LOG_FATAL("[BitcoinRpc::jsonCall] exception: %s", e.what());
    return 1;
  }
}

// 检测bitcoind是否工作正常
bool BitcoinRpc::CheckBitcoind() {
  string request("{\"id\":1,\"method\":\"getinfo\",\"params\":[]}");
  string response;
  int res = jsonCall(request, response, 5000); // 0: success
  if (res != 0) {
    LOG_FATAL("rpc call fail, bitcoind uri: %s", addr.c_str());
    return false;
  }
  JsonNode r;
  if (!JsonNode::parse(response.c_str(), response.c_str() + response.length(), r)) {
    LOG_FATAL("json parse failure: %s", response.c_str());
    return false;
  }
  JsonNode result = r["result"];
  if (result.type() == Utilities::JS::type::Null || result["connections"].int32() == 0) {
    LOG_FATAL("bitcoind is NOT works fine, getinfo: %s", response.c_str());
    return false;
  }
  return true;
}
