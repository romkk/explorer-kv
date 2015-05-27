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
#ifndef Explorer_Parser_h
#define Explorer_Parser_h

#include <string.h>

#include "Common.h"
#include "bitcoin/core.h"
#include "bitcoin/key.h"

void getRawBlockFromDisk(const int32_t height, string *rawHex,
                         int32_t *chainId, int64_t *blockId);

class RawBlock {
public:
  int64_t blockId_;
  int32_t height_;
  int32_t chainId_;
  uint256 hash_;
  char *hex_;

  RawBlock(const int64_t blockId, const int32_t height, const int32_t chainId,
           const uint256 hash, const char *hex);
  ~RawBlock();
};

struct AddrTx {
  int64_t txId_;
  int64_t totalReceived_;
  int64_t balanceDiff_;
  int64_t balanceFinal_;
  int32_t prevYmd_;
  int32_t nextYmd_;
  int64_t prevTxId_;
  int64_t nextTxId_;
  int32_t txHeight_;

  AddrTx() {
    memset((char *)&txId_, 0, sizeof(struct AddrTx));
  }
};

struct AddrInfo {
  char addrStr_[36];
  int64_t addrId_;
  int64_t txCount_;
  int32_t beginTxYmd_;
  int32_t endTxYmd_;
  int64_t beginTxId_;
  int64_t endTxId_;
  int64_t totalReceived_;
  int64_t totalSent_;

  struct AddrTx addrTx_;

  AddrInfo() {
    memset((char *)&addrStr_, 0, sizeof(struct AddrInfo));
  }
  bool operator<(const AddrInfo &val) const {
    int r = strncmp(addrStr_, val.addrStr_, 35);
    if (r < 0) {
      return true;
    }
    return false;
  }
};

class TxOutput {
public:
  vector<string> address_;
  vector<int64_t> addressIds_;
  int64_t value_;
  string  scriptHex_;
  string  scriptAsm_;
  string  typeStr_;

public:
  TxOutput(): value_(0) {}
  void operator=(const TxOutput &val) {
    address_    = val.address_;
    addressIds_ = val.addressIds_;
    value_      = val.value_;
    scriptHex_  = val.scriptHex_;
    scriptAsm_  = val.scriptAsm_;
    typeStr_    = val.typeStr_;
  }
};

struct TxInfo {
  uint256 hash256_;
  int64_t txId_;

  int32_t outputsCount_;
  TxOutput **outputs_;

  TxInfo(): hash256_(), txId_(0), outputsCount_(0), outputs_(nullptr) {
  }
  bool operator<(const TxInfo &val) const {
    return hash256_ < val.hash256_;
  }
};

class AddrHandler {
  vector<struct AddrInfo> addrInfo_;
  size_t addrCount_;

public:
  AddrHandler(const size_t addrCount, const string &file);
  vector<struct AddrInfo>::iterator find(const string &address);
  const int64_t getAddressId(const string &address);
};

class TxHandler {
  vector<struct TxInfo> txInfo_;
  size_t txCount_;

public:
  TxHandler(const size_t txCount, const string &file);
  vector<struct TxInfo>::iterator find(const uint256 &hash);
  vector<struct TxInfo>::iterator find(const string &hashStr);

  void addOutputs(CTransaction &tx, AddrHandler *addrHandler);
  void delOutput (const uint256 &hash, const int32_t n);
  class TxOutput *getOutput(const uint256 &hash, const int32_t n);
};

class PreParser {
  atomic<bool> running_;
  int32_t height_;
  mutex lockHeight_;

  AddrHandler *addrHandler_;
  TxHandler *txHandler_;

  string filePreTx_;
  string filePreAddr_;
  size_t addrCount_;
  size_t txCount_;

  int32_t stopHeight_;

  void initAddr();

public:
  PreParser();
  ~PreParser();

  void init();
  void run();
  void stop();
};

#endif
