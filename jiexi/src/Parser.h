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
#include "Util.h"
#include "MySQLConnection.h"
#include "bitcoin/core.h"
#include "bitcoin/key.h"

#include "SSDB_client.h"

#define TXLOG_STATUS_INIT 100
#define TXLOG_STATUS_DONE 1000

#define TXLOG_TYPE_ACCEPT   1
#define TXLOG_TYPE_ROLLBACK 2

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


////////////////////////////  LastestAddressInfo  ///////////////////////////////
class LastestAddressInfo {
public:
  int64_t addrId_;
  int32_t beginTxYmd_;
  int32_t endTxYmd_;
  int64_t beginTxId_;
  int64_t endTxId_;

  int64_t totalReceived_;
  int64_t totalSent_;

  int64_t unconfirmedReceived_;
  int64_t unconfirmedSent_;

  int32_t lastConfirmedTxYmd_;
  int64_t lastConfirmedTxId_;

  int64_t txCount_;
  time_t lastUseTime_;

  LastestAddressInfo(int64_t addrId,
                     int32_t beginTxYmd, int32_t endTxYmd,
                     int64_t beginTxId, int64_t endTxId,
                     int64_t unconfirmedReceived, int64_t unconfirmedSent,
                     int32_t lastConfirmedTxYmd, int64_t lastConfirmedTxId,
                     int64_t totalReceived, int64_t totalSent,
                     int64_t txCount);
  LastestAddressInfo(const LastestAddressInfo &a);
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
};


///////////////////////////////  CacheManager  /////////////////////////////////
// 若SSDB宕机，则丢弃数据
class CacheManager {
  atomic<bool> running_;
  atomic<bool> runningThreadConsumer_;
  atomic<bool> ssdbAlive_;

  ssdb::Client *ssdb_;
  string  SSDBHost_;
  int32_t SSDBPort_;

  mutex lock_;

  // 待删除队列，临时
  set<string> qkvTemp_;
  set<string> qhashsetTempSet_;  // 辅助去重
  vector<std::pair<string, string> > qhashsetTemp_;
  // 待删除队列，正式
  vector<string> qkv_;
  vector<std::pair<string, string> > qhashset_;

  // 待触发的URL列表
  string dirURL_;
  set<string>    qUrlTemp_;
  vector<string> qUrl_;

  void threadConsumer();

  void flushURL(vector<string> &buf);

public:
  CacheManager(const string  SSDBHost, const int32_t SSDBPort, const string dirURL);
  ~CacheManager();

  // non-thread safe
  void insertKV(const string &key);
  void insertHashSet(const string &address, const string &tableName);

  void commit();
};

///////////////////////////////  AddressTxNode  /////////////////////////////////
class AddressTxNode {
public:
  int32_t ymd_;  // 初始化、复制时使用 memcpy(), 保持 ymd_ 为第一个字段
  int32_t txHeight_;
  int64_t addressId_;
  int64_t txId_;
  int64_t totalReceived_;
  int64_t balanceDiff_;
  int64_t balanceFinal_;
  int64_t idx_;
  int32_t prevYmd_;
  int32_t nextYmd_;
  int64_t prevTxId_;
  int64_t nextTxId_;

  AddressTxNode();
  AddressTxNode(const AddressTxNode &node);
  void reset();
};

/////////////////////////////////  Parser  ////////////////////////////////////
class Parser {
private:
  atomic<bool> running_;
  MySQLConnection dbExplorer_;

  bool isReverse_;
  int64_t reverseEndTxlog2ID_;

  CacheManager *cache_;
  bool cacheEnable_;

  int64_t unconfirmedTxsSize_;
  int32_t unconfirmedTxsCount_;

  map<uint256/* tx hash */, map<int64_t/* addrID */, int64_t/* balance diff */> > addressBalanceCache_;

  bool tryFetchTxLog2(class TxLog2 *txLog2, const int64_t lastId);

  int64_t getLastTxLog2Id();
  void updateLastTxlog2Id(const int64_t newId);

  void checkTableAddressTxs(const uint32_t timestamp);

  // block
  void acceptBlock(TxLog2 *txLog2, string &blockHash);
  void rejectBlock(TxLog2 *txLog2);

  // tx
  void acceptTx   (class TxLog2 *txLog2);
  void confirmTx  (class TxLog2 *txLog2);
  void unconfirmTx(class TxLog2 *txLog2);
  void rejectTx   (class TxLog2 *txLog2);

  // 操作 tx 的辅助函数
  void _getAddressTxNode(const int64_t txId,
                         const LastestAddressInfo *addr, AddressTxNode *node);
  void _removeAddressTxNode(LastestAddressInfo *addr, AddressTxNode *node);

  void _rejectAddressTxs(class TxLog2 *txLog2, const map<int64_t, int64_t> &addressBalance);

  // 未确认交易池
  void addUnconfirmedTx   (class TxLog2 *txLog2);
  void removeUnconfirmedTx(class TxLog2 *txLog2);

  // 移动地址交易节点
  void _switchAddressTxNode(AddressTxNode *prevNode, AddressTxNode *currNode);
  void moveForwardAddressTxNode (const LastestAddressInfo *addr, AddressTxNode *node);
  void moveBackwardAddressTxNode(const LastestAddressInfo *addr, AddressTxNode *node);

  void writeLastProcessTxlogTime();

public:
  Parser();
  ~Parser();

  bool init();
  void run();
  void stop();

  void setReverseMode(const int64_t endTxlogID);

  bool txsHash2ids(const std::set<uint256> &hashVec,
                   std::map<uint256, int64_t> &hash2id);

};

#endif
