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


class TxLog {
public:
  int64_t  logId_;     // id in table
  int32_t  tableIdx_;  // txlogs table name index
  int32_t  status_;    // handle status
  int32_t  type_;      // 1: accept, 2: rollback
  int32_t  blkHeight_;
  uint32_t blockTimestamp_;
  string  createdAt_;

  string txHex_;
  uint256 txHash_;
  CTransaction tx_;
  int64_t txId_;

  TxLog();
  ~TxLog();
};


class Parser {
private:
  atomic<bool> running_;
  MySQLConnection dbExplorer_;

  bool tryFetchLog(class TxLog *txLog, const int64_t lastTxLogOffset);
  int64_t getLastTxLogOffset();

  int32_t getTxLogMaxIdx();
  void updateLastTxlogId(const int64_t newId);
  void checkTableAddressTxs(const uint32_t timestamp);

  void acceptBlock  (const int32_t height);
  void rollbackBlock(const int32_t height);

  void acceptTx  (class TxLog *txLog);
  void rollbackTx(class TxLog *txLog);

public:
  Parser();
  ~Parser();

  bool init();
  void run();
  void stop();

  bool txsHash2ids(const std::set<uint256> &hashVec,
                   std::map<uint256, int64_t> &hash2id);

};

bool multiInsert(MySQLConnection &db, const string &table,
                 const string &fields, const vector<string> &values);

#endif
