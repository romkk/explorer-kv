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
#include "bitcoin/core.h"
#include "bitcoin/key.h"

#define TXLOG_STATUS_INIT 100
#define TXLOG_STATUS_DONE 1000

#define TXLOG_TYPE_ACCEPT   1
#define TXLOG_TYPE_ROLLBACK 2

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

class LastestAddressInfo {
public:
  int32_t beginTxYmd_;
  int32_t endTxYmd_;
  int64_t beginTxId_;
  int64_t endTxId_;
  int64_t totalReceived_;
  int64_t totalSent_;

  LastestAddressInfo(int32_t beginTxYmd, int32_t endTxYmd, int64_t beginTxId,
                     int64_t endTxId, int64_t totalReceived, int64_t totalSent);
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

class AddrTxCache {
  pthread_mutex_t lock_;
  std::unordered_map<int64_t, bool> using_;
  std::unordered_map<int64_t, LastestAddressInfo *> cache_;

public:
  AddrTxCache();
  ~AddrTxCache();

  std::unordered_map<int64_t, LastestAddressInfo *>::const_iterator end() const;
  std::unordered_map<int64_t, LastestAddressInfo *>::iterator find(const int64_t addrId);
  void insert(const int64_t addrId, LastestAddressInfo *ptr);

  bool lockAddr(const int64_t addrId);
  void unlockAddr(const int64_t addrId);
};

class BlockTx {
public:
  int64_t txId_;
  int32_t height_;
  uint32_t nTime_;  // block timestamp
  uint256 txHash_;
  CTransaction tx_;
  int32_t txSize_;

  BlockTx(int64_t txId, int32_t height, uint32_t nTime, const uint256 &txHash, const CTransaction &tx, int32_t size);
};

class Parser {
private:
  atomic<bool> running_;
  string dbUri_;
  MySQLConnection dbExplorer_;

  vector<MySQLConnection *>  poolDB_;
  vector<vector<BlockTx *> > poolBlockTx_;
  int32_t threadsNumber_;

  vector<atomic<int32_t> > runThreads_;

  map<string, int64_t> curBlockAddrMap_;

  int32_t getLastHeight();
  void updateLastHeight(const int32_t newHeight);

  void checkTableAddressTxs(const uint32_t timestamp);

  void acceptBlock(const int32_t height);

  void acceptTxThread(const int32_t i);
  void acceptTx(MySQLConnection *db, const BlockTx *blkTx);

public:
  Parser();
  ~Parser();

  bool init();
  void run();
  void stop();
};

bool multiInsert(MySQLConnection &db, const string &table,
                 const string &fields, const vector<string> &values);

#endif
