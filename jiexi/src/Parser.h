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


#include "Common.h"
#include "MySQLConnection.h"
#include "Log2Producer.h"
#include "Util.h"

#include "inotify-cxx.h"

#include "bitcoin/core.h"
#include "bitcoin/key.h"

#include "KVDB.h"
#include "HttpAPI.h"
#include "RecognizeBlock.h"

// tparser的异常，整数
#define EXCEPTION_TPARSER_TX_INVALID_INPUT 100

//
// 为降低交易链日期间移动节点的复杂度，我们把未确认的交易都设置为未来时间: 2030-01-01
// 交易进来的时候(accept)都会放到那个表里，然后确认时(confirm)再迁移回来。
//
// 1893456000 -> 2030-01-01 00:00:00
//
#define UNCONFIRM_TX_YMD        20300101
#define UNCONFIRM_TX_TIMESTAMP  1893456000

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


void _insertOrphanBlockHash(const string &hash, string &value);
void _removeOrphanBlockHash(const string &hash, string &value);

/////////////////////////////////  RawBlock  ////////////////////////////////////
class RawBlock {
public:
  int64_t blockId_;
  int32_t height_;
  int32_t chainId_;
  uint256 hash_;
  string  hex_;

  RawBlock(const int64_t blockId, const int32_t height, const int32_t chainId,
           const uint256 hash, const string &hex);
};


////////////////////////////  AddressInfo  ///////////////////////////////
class AddressInfo {
public:
  int32_t txCount_;
  int64_t received_;
  int64_t sent_;

  int32_t unconfirmedTxCount_;
  int64_t unconfirmedReceived_;
  int64_t unconfirmedSent_;

  int32_t unspentTxCount_;  // 未花费的数量
  int32_t unspentTxIndex_;  // 当前最大未花费的索引，每次来都加一

  int32_t lastConfirmedTxIdx_;

  int64_t lastUseIdx_;  // 用于清理缓存，和address本身无关

  AddressInfo(const int64_t lastUseIdx);
  AddressInfo(const int32_t txCount, const int64_t received, const int64_t sent,
              const int32_t unconfirmedTxCount, const int64_t unconfirmedReceived, const int64_t unconfirmedSent,
              const int32_t unspentTxCount, const int32_t unspentTxIndex,
              const int32_t lastConfirmedTxIdx,
              const int64_t lastUseIdx);
  AddressInfo(const AddressInfo &a);
};

class DBTxOutput {
public:
  int64_t txId;
  int32_t position;
  int64_t value;
  int64_t spentTxId;
  int32_t spentPosition;
  string  address;
  string  addressIds;

  DBTxOutput(): txId(0), position(-1), value(-1), spentTxId(0), spentPosition(-1) {
  }
};


/////////////////////////////////  TxLog2  ////////////////////////////////////
//
// 对应 table.0_txlogs2
//
class TxLog2 {
public:
  int64_t  id_;         // id, auto_increment
  int32_t  type_;       // 100: TX_ACCEPT
                        // 200: TX_CONFIRM
                        // 300: TX_UNCONFIRM
                        // 400: TX_REJECT
  int32_t  blkHeight_;     // block height
  int64_t  blkId_;         // block ID

  uint32_t maxBlkTimestamp_;  // max block timestamp (修正后的块时间戳)
  int32_t  ymd_;              // 时间戳所对应的 ymd，对应 address_tx_<yyyymm>

  string   createdAt_;

  string txHex_;
  uint256 txHash_;
  CTransaction tx_;
  int64_t txId_;

  TxLog2();
  TxLog2(const TxLog2 &t);
  ~TxLog2();

  string toString() const;
  bool isEmpty();
};


///////////////////////////////  AddressTxNode  /////////////////////////////////
class AddressTxNode {
public:
  uint32_t txBlockTime_;  // 初始化、复制时使用 memcpy(), 保持 txBlockTime_ 为第一个字段
  int32_t  txHeight_;
  int64_t  balanceDiff_;
  int32_t  idx_;

  AddressTxNode();
  AddressTxNode(const AddressTxNode &node);
  void reset();
};

///////////////////////////////  TxInfo  /////////////////////////////////
class TxInfo {
public:
  int64_t txId_;
  string  hex_;
  int32_t count_;
  time_t  useTime_;

  TxInfo(const int64_t txId, const string &hex);
  TxInfo(const TxInfo &t);
  TxInfo();
};


///////////////////////////////  TxInfoCache  /////////////////////////////////
class TxInfoCache {
  time_t lastClearTime_;
  map<uint256, TxInfo> cache_;

public:
  TxInfoCache();
  void getTxInfo(MySQLConnection &db, const uint256 hash, int64_t *txId, string *hex);
};


/////////////////////////////////  Parser  ////////////////////////////////////
class Parser {
private:
  mutex lock_;
  Condition newTxlogs_;

  atomic<bool> running_;

  int64_t unconfirmedTxsSize_;
  int32_t unconfirmedTxsCount_;

  // kv
  KVDB kvdb_;

  // 交易信息(id, hex等）缓存
  TxInfoCache txInfoCache_;

  // notify
  string notifyFileLog2Producer_;
  Inotify inotify_;
  thread threadWatchNotify_;
  void threadWatchNotifyFile();  // 监听log2：NOTIFY_LOG2_TO_TPARSER

  // API Httpd
  APIServer apiServer_;

  // 处理 txlogs
  thread threadHandleTxlogs_;
  void threadHandleTxlogs();

  // 块识别
  RecognizeBlock recognizeBlock_;
  thread threadRecognizeBlock_;
  void threadRecognizeBlock();

  // 生成 txlogs
  BoundedBuffer<TxLog2> txlogsBuffer_;
  thread threadProduceTxlogs_;
  void threadProduceTxlogs();

  bool tryFetchTxLog2FromDB(class TxLog2 *txLog2, const int64_t lastId, MySQLConnection &dbExplorer);

  int64_t getLastTxLog2Id();
  void updateLastTxlog2Id(const int64_t newId);

  // block
  void acceptBlock(TxLog2 *txLog2);
  void rejectBlock(TxLog2 *txLog2);

  // tx
  void acceptTx   (class TxLog2 *txLog2);
  void confirmTx  (class TxLog2 *txLog2);
  void unconfirmTx(class TxLog2 *txLog2);
  void rejectTx   (class TxLog2 *txLog2);

  // flush address info
  void flushAddressInfo(const map<string, int64_t> &addressBalance);

  // 获取tx对应各个地址的余额变更情况
  map<string, int64_t> getTxAddressBalance(const CTransaction &tx);

  // 操作 tx 的辅助函数
  void _getAddressTxNode(const string &address, const uint256 &txhash,
                         AddressTxNode *node);
  void _removeAddressTxNode(const string &address, const int32_t idx,
                            const uint256 &txhash, const int64_t balanceDiff,
                            AddressInfo *addr);

  void _rejectAddressTxs(class TxLog2 *txLog2, const map<string, int64_t> &addressBalance);

  // 确认交易节点 & 反确认
  void _confirmAddressTxNode  (const string &address, AddressTxNode *node, AddressInfo *addr);
  void _unconfirmAddressTxNode(const string &address, AddressTxNode *node, AddressInfo *addr);

  // 未确认交易池
  void addUnconfirmedTxPool   (class TxLog2 *txLog2);
  void removeUnconfirmedTxPool(class TxLog2 *txLog2);

  // 更新交易 / 节点的 YMD
  void _updateTxNodeYmd(AddressInfo *addr, AddressTxNode *node, const int32_t targetYmd);

  // 交换地址交易节点
  void _switchTxNode(const string &address, const int32_t idx1, const int32_t idx2);
  void _rejectTx_MoveToLastUnconfirmed(AddressInfo *addr, AddressTxNode *node);

  void writeLastProcessTxlogTime();


public:
  Parser();
  ~Parser();

  bool init();
  void run();
  void stop();

  void txsHash2ids(const std::set<uint256> &hashVec,
                   std::map<uint256, int64_t> &hash2id);

};

#endif
