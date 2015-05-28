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
  int64_t balanceDiff_;
  int64_t balanceFinal_;
  int32_t prevYmd_;
  int32_t nextYmd_;
  int64_t prevTxId_;
  int64_t nextTxId_;
  int32_t txHeight_;
  int32_t ymd_;

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
  int64_t spentTxId_;
  int32_t spentPosition_;

public:
  TxOutput(): value_(0), spentTxId_(0), spentPosition_(-1) {}
  void operator=(const TxOutput &val) {
    address_    = val.address_;
    addressIds_ = val.addressIds_;
    value_      = val.value_;
    scriptHex_  = val.scriptHex_;
    scriptAsm_  = val.scriptAsm_;
    typeStr_    = val.typeStr_;
    spentTxId_  = val.spentTxId_;
    spentPosition_  = val.spentPosition_;
  }
};

struct TxInfo {
  uint256 hash256_;
  int64_t txId_;

  int32_t blockHeight_;
  int32_t outputsCount_;
  TxOutput **outputs_;

  TxInfo(): hash256_(), txId_(0), blockHeight_(-1), outputsCount_(0), outputs_(nullptr) {
  }
  bool operator<(const TxInfo &val) const {
    return hash256_ < val.hash256_;
  }
};

struct BlockInfo {
  int64_t blockId_;
  uint256 blockHash_;
  CBlockHeader header_;
  int32_t height_;
  int64_t prevBlockId_;
  int64_t nextBlockId_;
  uint256 nextBlockHash_;
  int32_t chainId_;
  int32_t size_;
  uint64_t diff_;
  int32_t txCount_;
  int64_t rewardBlock_;
  int64_t rewardFee_;

  BlockInfo() {
    memset((char *)blockId_, 0, sizeof(struct BlockInfo));
  }
};

class AddrHandler {
  vector<struct AddrInfo> addrInfo_;
  size_t addrCount_;

public:
  AddrHandler(const size_t addrCount, const string &file);
  vector<struct AddrInfo>::iterator find(const string &address);
  int64_t getAddressId(const string &address);
  void dumpTxs(map<int32_t, FILE *> &fAddrTxs);
};

class TxHandler {
  vector<struct TxInfo> txInfo_;
  size_t txCount_;

public:
  TxHandler(const size_t txCount, const string &file);
  vector<struct TxInfo>::iterator find(const uint256 &hash);
  vector<struct TxInfo>::iterator find(const string &hashStr);

  int64_t getTxId(const uint256 &hash);

  void addOutputs(const CTransaction &tx,
                  AddrHandler *addrHandler, const int32_t height,
                  map<string, int64_t> &addressBalance);
  void delOutput (const uint256 &hash, const int32_t n);
  class TxOutput *getOutput(const uint256 &hash, const int32_t n);

  void dumpUnspentOutputToFile(vector<FILE *> &fUnspentOutputs);
};

class PreParser {
  atomic<bool> running_;
  int32_t curHeight_;

  BlockInfo blockInfo_;

  AddrHandler *addrHandler_;
  TxHandler   *txHandler_;

  FILE *fBlocks_;
  vector<FILE *>       fBlockTxs_;
  vector<FILE *>       fUnspentOutputs_;
  map<int32_t, FILE *> fAddrTxs_;

  string filePreTx_;
  string filePreAddr_;
  size_t addrCount_;
  size_t txCount_;

  int32_t stopHeight_;

  // parse block
  void parseBlock(const CBlock &blk, const int64_t blockId,
                  const int32_t height, const int32_t blockBytes,
                  const int32_t chainId);

  // parse TX
  void parseTx(const int32_t height, const CTransaction &tx, const uint32_t nTime);
  void parseTxInputs(const CTransaction &tx, const int64_t txId,
                     int64_t &valueIn, map<string, int64_t> &addressBalance);
  void parseTxSelf(const int32_t height, const int64_t txId, const uint256 &txHash,
                   const CTransaction &tx, const int64_t valueIn,
                   const uint32_t ntime);
  void handleAddressTxs(const map<string, int64_t> &addressBalance,
                        const int64_t txId, const int32_t ymd, const int32_t height);

  void parseBlocks();
  void cleanup();

public:
  PreParser();
  ~PreParser();

  void init();
  void run();
  void stop();
};

#endif
