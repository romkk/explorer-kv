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

#include "parser.h"
#include "Common.h"

#include "bitcoin/base58.h"

//
// blkHeight:
//   -2   无效的块高度
//   -1   临时块（未确认的交易组成的虚拟块）
//   >= 0 有效的块高度
//
TxLog::TxLog():logId_(0), tableIdx_(-1), status_(0), type_(0), blkHeight_(-2) {
}
TxLog::~TxLog() {}


Parser::Parser():dbExplorer_(Config::GConfig.get("db.explorer.uri")),
running_(true) {
}


bool Parser::init() {
  if (!dbExplorer_.ping()) {
    LOG_FATAL("connect to explorer DB failure");
    return false;
  }
  return true;
}

void Parser::run() {

  while (running_) {
    TxLog txlog;
    if (tryFetchLog(&txlog) == false) {
      sleepMs(1000);
      continue;
    }


  } /* /while */
}


void get_inputs_txs(const CTransaction &tx, std::set<uint256> &hashVec) {
  hashVec.clear();
  for (auto &it : tx.vin) {
    hashVec.insert(it.prevout.hash);
  }
}

// hash(uint256) -> (id)int64
bool Parser::txs_hash2id(const std::set<uint256> &hashVec,
                         std::map<uint256, int64_t> &hash2id) {
  string sql = "SELECT `id`,`hash` FROM `txs` WHERE `hash` IN (";
  for (auto &it : hashVec) {
    sql += "'" + it.ToString() + "',";
  }
  sql.resize(sql.length() - 1);  // remove last ','
  sql += ")";

  MySQLResult res;
  char **row;
  dbExplorer_.query(sql, res);
  if (hashVec.size() != res.numRows()) {
    LOG_FATAL("inputs in db is not match, db: %llu, tx: %llu ",
              res.numRows(), hashVec.size());
    return false;
  }

  while ((row = res.nextRow()) != nullptr) {
    hash2id.insert(std::make_pair(uint256(row[1]), atoi64(row[0])));
  }
  assert(hash2id.size() == hashVec.size());

  return true;
}

//bool Parser::accept_tx_inputs(const CTransaction &tx) {
//
//}
//
//void Parser::accept_tx_outputs(const CTransaction &tx) {
//
//}

void Parser::addressChanges(const CTransaction &tx,
                            vector<std::pair<CTxDestination, int64_t> > &items) {


  // 处理输出
  vector<std::pair<uint256, uint32_t> > outputs;
  for (auto &it : tx.vout) {
    if (it.IsNull() || it.scriptPubKey.IsUnspendable()) {
      continue;
    }

    txnouttype type;
    std::vector<CTxDestination> vDest;
    int nRequired;
    if (ExtractDestinations(it.scriptPubKey, type, vDest, nRequired)) {
      // TODO: support TX_MULTISIG
      if (nRequired == 1) {
        CBitcoinAddress address(vDest[0]);
      }
    } else {
      LOG_WARN("ExtractDestinations fail, txhash: %s", tx.GetHash().ToString().c_str());
    }
  }
}

int32_t Parser::getTxLogMaxIdx() {
  MySQLResult res;
  string sql = "SELECT `value` FROM `a_explorer_meta` WHERE `key` = 'latest_txlogs_tablename_index' ";
  char **row = nullptr;

  dbExplorer_.query(sql, res);
  if (res.numRows() == 1) {
    row = res.nextRow();
    return atoi(row[0]);
  }
  return 0;  // default value is zero
}

void Parser::updateLastTxlogId(const int64_t newId) {
  string sql = Strings::Format("UPDATE `a_explorer_meta` SET `value` = %lld "
                               " WHERE `key`='jiexi.last_txlog_offset'",
                               newId);
  if (!dbExplorer_.execute(sql.c_str())) {
    LOG_ERROR("failed to update 'jiexi.last_txlog_offset' to %lld", newId);
  }
}

int64_t Parser::getLastTxLogOffset() {
  MySQLResult res;
  int64_t lastTxLogOffset = 0;
  char **row = nullptr;
  string sql;

  // find last tx log ID
  sql = "SELECT `value` FROM `a_explorer_meta` WHERE `key`='jiexi.last_txlog_offset'";
  dbExplorer_.query(sql, res);
  if (res.numRows() == 1) {
    row = res.nextRow();
    lastTxLogOffset = strtoll(row[0], nullptr, 10);
    assert(lastTxLogOffset > 0);
  } else {
    lastTxLogOffset = 0;  // default value is zero
  }
  return lastTxLogOffset;
}

bool Parser::tryFetchLog(class TxLog *txLog, const int64_t lastTxLogOffset) {
  MySQLResult res;
  int32_t tableNameIdx = -1, logId = -1;
  char **row = nullptr;
  string sql;

  // id / 100000000 是表名称索引
  tableNameIdx = (int32_t)(lastTxLogOffset / 100000000);
  // id % 100000000 是该表中的id序列
  logId        = (int32_t)(lastTxLogOffset % 100000000);

  // fetch tx log, 每次处理一条日志记录
  sql = Strings::Format(" SELECT `id`,`handle_status`,`handle_type`, "
                        "   `block_height`,`tx_hash`,`created_at` "
                        " FROM `txlogs_%04d` "
                        " WHERE `id` > %d ORDER BY `id` ASC LIMIT 1 ",
                        tableNameIdx, logId);
  dbExplorer_.query(sql, res);
  if (res.numRows() == 0) {
    // 检测是否发生txlog表切换
    // 若读取不到数据且表名索引出现新的，则更新 'jiexi.last_txlog_offset'
    int32_t txLogMaxIdx = getTxLogMaxIdx();
    assert(txLogMaxIdx >= tableNameIdx);

    if (txLogMaxIdx > tableNameIdx) {
      updateLastTxlogId((tableNameIdx + 1)*100000000 + 0);
      LOG_INFO("switch table.txlogs from `txlogs_%04d` to `txlogs_%04d`",
               tableNameIdx, tableNameIdx + 1);
    } else if (txLogMaxIdx < tableNameIdx) {
      LOG_FATAL("table.txlogs name index(%d) is too larger than exist(%d)",
                tableNameIdx, txLogMaxIdx);
    } else {
      // do nothing
      assert(txLogMaxIdx == tableNameIdx);
    }
    return false;
  }

  row = res.nextRow();
  txLog->tableIdx_  = tableNameIdx;
  txLog->logId_     = strtoll(row[0], nullptr, 10);
  txLog->status_    = atoi(row[1]);
  txLog->type_      = atoi(row[2]);
  txLog->blkHeight_ = atoi(row[3]);
  txLog->txHash_    = uint256(row[4]);
  txLog->createdAt_ = string(row[5]);

  if (txLog->status_ != TXLOG_STATUS_INIT) {
    LOG_FATAL("invalid status: %d", txLog->status_);
    return false;
  }
  if (txLog->type_ != TXLOG_TYPE_ACCEPT && txLog->type_ != TXLOG_TYPE_ROLLBACK) {
    LOG_FATAL("invalid type: %d", txLog->status_);
    return false;
  }

  LOG_INFO("process txlog, tableIdx: %d, logId: %d, type: %d, "
           "height: %d, tx hash: %s, created: %s",
           tableNameIdx, logId, txLog->type_, txLog->blkHeight_,
           txLog->txHash_.ToString().c_str(), txLog->createdAt_.c_str());

  // find raw tx hex
  string txHashStr = txLog->txHash_.ToString();
  sql = Strings::Format("SELECT `hex` FROM `raw_txs` WHERE `tx_hash` = '%s' ",
                        txHashStr.c_str());
  dbExplorer_.query(sql, res);
  if (res.numRows() == 0) {
    LOG_FATAL("can't find raw tx by hash: %s", txHashStr.c_str());
    return false;
  }
  row = res.nextRow();
  txLog->txHex_ = string(row[0]);

  // parse hex string from parameter
  vector<unsigned char> txData(ParseHex(txLog->txHex_));
  CDataStream ssData(txData, SER_NETWORK, BITCOIN_PROTOCOL_VERSION);

  // deserialize binary data stream
  try {
    ssData >> txLog->tx_;
  }
  catch (std::exception &e) {
    LOG_FATAL("TX decode failed: %s", e.what());
    return false;
  }
  if (txLog->tx_.GetHash() != txLog->txHash_) {
    LOG_FATAL("TX decode failed, hash is not match, db: %s, calc: %s",
              txLog->txHash_.ToString().c_str(),
              txLog->tx_.GetHash().ToString().c_str());
    return false;
  }

  return true;
}

