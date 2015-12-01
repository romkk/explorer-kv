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
#include "Util.h"

#include "bitcoin/core.h"
#include "bitcoin/key.h"

#include "rocksdb/db.h"
#include "rocksdb/slice.h"
#include "rocksdb/options.h"


#define KVDB_PREFIX_TX_RAW_HEX   "00_"
#define KVDB_PREFIX_TX_OBJECT    "01_"
#define KVDB_PREFIX_TX_SPEND     "02_"

#define KVDB_PREFIX_BLOCK_HEIGHT     "10_"
#define KVDB_PREFIX_BLOCK_OBJECT     "11_"
#define KVDB_PREFIX_BLOCK_TXS_STR    "12_"

#define KVDB_PREFIX_ADDR_OBJECT     "20_"
#define KVDB_PREFIX_ADDR_TX         "21_"
#define KVDB_PREFIX_ADDR_TX_INDEX   "22_"
#define KVDB_PREFIX_ADDR_UNSPENT    "23_"
#define KVDB_PREFIX_ADDR_UNSPENT_INDEX  "24_"


void getRawBlockFromDisk(const int32_t height, string *rawHex,
                         int32_t *chainId, int64_t *blockId);

inline int32_t tableIdx_Addr(const int64_t addrId) {
  return (int32_t)(addrId / BILLION % 64);
}
inline int32_t tableIdx_AddrUnspentOutput(const int64_t addrId) {
  return (int32_t)(addrId % 10);
}
inline int32_t tableIdx_TxOutput(const int64_t txId) {
  return (int32_t)(txId % 100);
}
inline int32_t tableIdx_TxInput(const int64_t txId) {
  return (int32_t)(txId % 100);
}
inline int32_t tableIdx_Tx(const int64_t txId) {
  return (int32_t)(txId / BILLION % 64);
}
inline int32_t tableIdx_AddrTxs(const int32_t ymd) {
  // 按月分表： 20150515 -> 201505
  return ymd / 100;
}
inline int32_t tableIdx_BlockTxs(const int64_t blockId) {
  return (int32_t)(blockId % 100);
}

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
  int64_t  balanceDiff_;
  int32_t  txHeight_;
  uint32_t txBlockTime_;  // 交易所在的块时间，未确认为零
  uint256  txHash_;

  AddrTx() {
    memset((char *)&balanceDiff_, 0, sizeof(struct AddrTx));
    txHeight_ = -1;
  }
};

struct AddrInfo {
  char addrStr_[36];
  int64_t received_;
  int64_t sent_;
  int32_t txCount_;
  int32_t unspentTxCount_;

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
  int64_t value_;
  string  scriptHex_;
  string  scriptAsm_;
  string  typeStr_;

public:
  TxOutput(): value_(0) {}
  void operator=(const TxOutput &val) {
    address_    = val.address_;
    value_      = val.value_;
    scriptHex_  = val.scriptHex_;
    scriptAsm_  = val.scriptAsm_;
    typeStr_    = val.typeStr_;
  }
};

struct TxInfo {
  uint256 hash256_;

  int32_t blockHeight_;
  int32_t outputsCount_;
  TxOutput **outputs_;

  TxInfo(): hash256_(), blockHeight_(-1), outputsCount_(0), outputs_(nullptr) {
  }
  bool operator<(const TxInfo &val) const {
    return hash256_ < val.hash256_;
  }
};

struct BlockInfo {
  int32_t height_;  // 初始化 memset 使用了该字段
  uint256 blockHash_;
  CBlockHeader header_;
  uint256 nextBlockHash_;
  int32_t size_;
  double  diff_;
  int32_t txCount_;
  int64_t rewardBlock_;
  int64_t rewardFee_;

  BlockInfo() {
    memset((char *)&height_, 0, sizeof(struct BlockInfo));
  }
};

class AddrHandler {
  vector<struct AddrInfo> addrInfo_;
  size_t addrCount_;

public:
  AddrHandler(const size_t addrCount, const string &file);
  ~AddrHandler();
  vector<struct AddrInfo>::iterator find(const string &address);
  void dumpAddressAndTxs(map<int32_t, FILE *> &fAddrTxs, vector<FILE *> &fAddrs_);
};

class TxHandler {
  vector<struct TxInfo> txInfo_;
  size_t txCount_;

public:
  TxHandler(const size_t txCount, const string &file);
  ~TxHandler();

  vector<struct TxInfo>::iterator find(const uint256 &hash);
  vector<struct TxInfo>::iterator find(const string &hashStr);

  void addOutputs(const CTransaction &tx, const int32_t height);
  void delOutput(const uint256 &hash, const int32_t n);
  void delOutput(TxInfo &txInfo, const int32_t n);
  void delOutputAll(TxInfo &txInfo);
  class TxOutput *getOutput(const uint256 &hash, const int32_t n);

  void dumpUnspentOutputToFile();
};

///////////////////////////////  BlockTimestamp  /////////////////////////////////
class BlockTimestamp {
  int32_t limit_;
  int64_t currMax_;
  map<int32_t, int64_t> blkTimestamps_;  // height <-> timestamp

public:
  BlockTimestamp(const int32_t limit);
  int64_t getMaxTimestamp() const;
  void pushBlock(const int32_t height, const int64_t ts);
  void popBlock();
};


/////////////////////////////////  KVHandler  //////////////////////////////////

class KVHandler {
  atomic<bool> running_;
  mutex lock_;

  time_t startTime_;
  int64_t counter_;

  // rocksdb
  rocksdb::DB *db_;
  rocksdb::Options options_;
  rocksdb::WriteOptions writeOptions_;

  // buffer, 采用 vecotr ，vector中元素为 string 时，会导致内存上升过快，得不到释放
  vector<uint8_t> keysData_;
  vector<int32_t> keysLength_;
  vector<int32_t> keysOffset_;

  vector<uint8_t> valuesData_;
  vector<int32_t> valuesLength_;
  vector<int32_t> valuesOffset_;

  vector<uint8_t> tmpKeysData_;
  vector<int32_t> tmpKeysLength_;
  vector<int32_t> tmpKeysOffset_;

  vector<uint8_t> tmpValuesData_;
  vector<int32_t> tmpValuesLength_;
  vector<int32_t> tmpValuesOffset_;


  void printSpeed();

  atomic<int32_t> runningConsumeThreads_;
  void threadConsumeKVItems();
  size_t writeToDisk();

public:
  KVHandler();
  ~KVHandler();

  void set(const string &key, const string &value);
  void set(const string &key, const uint8_t *data, const size_t length);

  void start();
  void stop();
};

/////////////////////////////////  PreParser  //////////////////////////////////

class PreParser {
  atomic<bool> running_;
  int32_t curHeight_;

  BlockInfo blockInfo_;

  AddrHandler *addrHandler_;
  TxHandler   *txHandler_;

  string filePreTx_;
  string filePreAddr_;
  size_t addrCount_;
  size_t txCount_;

  int32_t stopHeight_;

  // 块最大时间戳
  BlockTimestamp blkTs_;

  // parse block
  void parseBlock(const CBlock &blk, const int64_t blockId,
                  const int32_t height, const int32_t blockBytes);

  // parse TX
  void parseTx(const int32_t height, const CTransaction &tx, const uint32_t blockNTime);
  void handleAddressTxs(const map<string, int64_t> &addressBalance,
                        const int32_t height, const uint32_t blockTime,
                        const uint256 txHash);

  // help functions
  void _saveBlock(const BlockInfo &b);
  void _insertBlockTxs(const CBlock &blk);

public:
  PreParser();
  ~PreParser();

  void init();
  void run();
  void stop();
};

#endif
