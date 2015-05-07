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

#include "Parser.h"
#include "Common.h"

#include "bitcoin/base58.h"
#include "bitcoin/util.h"

//static bool getBlockRawHexByHeight(const int height,    MySQLConnection &db, string *rawHex);
static bool getBlockRawHexByHash(const uint256 &hash, MySQLConnection &db,
                                 string *rawHex, int32_t *chainId, int64_t *blockId);


//bool getBlockRawHexByHeight(const int height, MySQLConnection &db, string *rawHex) {
//  MySQLResult res;
//  string sql = Strings::Format("SELECT `hex` FROM `0_raw_blocks` WHERE "
//                               " `block_height` = %d AND `chain_id` = 0",
//                               height);
//  char **row = nullptr;
//
//  db.query(sql, res);
//  if (res.numRows() == 1) {
//    row = res.nextRow();
//    rawHex->assign(row[0]);
//    assert(rawHex->length() % 2 == 0);
//    return true;
//  }
//
//  return false;
//}

bool getBlockRawHexByHash(const uint256 &hash, MySQLConnection &db,
                          string *rawHex, int32_t *chainId, int64_t *blockId) {
  MySQLResult res;
  string sql = Strings::Format("SELECT `hex`,`chain_id`,`id` FROM `0_raw_blocks`  "
                               " WHERE `block_hash` = %s ",
                               hash.ToString().c_str());
  char **row = nullptr;

  db.query(sql, res);
  if (res.numRows() == 1) {
    row = res.nextRow();
    if (rawHex != nullptr) {
      rawHex->assign(row[0]);
      assert(rawHex->length() % 2 == 0);
    }
    if (chainId != nullptr) {
      *chainId = atoi(row[1]);
    }
    if (blockId != nullptr) {
      *blockId  = atoi64(row[2]);
    }
    return true;
  }

  return false;
}

// 批量插入函数
bool multiInsert(MySQLConnection &db, const string &table,
                 const string &fields, const vector<string> &values) {
  int32 dbMaxAllowedPacket = (int32)atol(db.getVariable("max_allowed_packet").c_str());
  string sqlPrefix = Strings::Format("INSERT INTO `%s`(%s) VALUES ",
                                     table.c_str(), fields.c_str());

  if (values.size() == 0 || fields.length() == 0 || table.length() == 0) {
    return false;
  }

  string sql = sqlPrefix;
  for (auto &it : values) {
    sql += Strings::Format("(%s),", it.c_str());
    // 超过DB限制，或者超过 4MB
    size_t size = sql.length();
    if (size > dbMaxAllowedPacket || size > 4*1024*1024) {
      sql.resize(sql.length() - 1);  // 去掉最后一个逗号
      if (!db.execute(sql.c_str())) {
        return false;
      }
      sql = sqlPrefix;
    }
  }

  // 最后剩余的一批
  if (sql.length() > sqlPrefix.length()) {
    sql.resize(sql.length() - 1);  // 去掉最后一个逗号
    if (!db.execute(sql.c_str())) {
      return false;
    }
  }

  return true;
}

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
//    if (tryFetchLog(&txlog) == false) {
//      sleepMs(1000);
//      continue;
//    }


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
  string sql = "SELECT `value` FROM `0_explorer_meta` WHERE `key` = 'latest_txlogs_tablename_index' ";
  char **row = nullptr;

  dbExplorer_.query(sql, res);
  if (res.numRows() == 1) {
    row = res.nextRow();
    return atoi(row[0]);
  }
  return 0;  // default value is zero
}

void Parser::updateLastTxlogId(const int64_t newId) {
  string sql = Strings::Format("UPDATE `0_explorer_meta` SET `value` = %lld "
                               " WHERE `key`='jiexi.last_txlog_offset'",
                               newId);
  if (!dbExplorer_.execute(sql.c_str())) {
    LOG_ERROR("failed to update 'jiexi.last_txlog_offset' to %lld", newId);
  }
}

bool _insertBlock(MySQLConnection &db, const CBlock &blk,
                  const int64_t blockId, const int32_t height,
                  const int32_t blockBytes) {
  CBlockHeader header = blk.GetBlockHeader();  // alias
  string prevBlockHash = header.hashPrevBlock.ToString();
  MySQLResult res;
  char **row = nullptr;

  // 查询前向块信息，前向块信息必然存在
  int64_t prevBlockId = 0;
  {
    string sql = Strings::Format("SELECT `block_id` FROM `0_blocks` WHERE `hash` = '%s'",
                                 prevBlockHash.c_str());
    db.query(sql, res);
    if (res.numRows() != 1) {
      LOG_ERROR("prev block not exist in DB, hash: ", prevBlockHash.c_str());
      return false;
    }
    row = res.nextRow();
    prevBlockId = atoi64(row[0]);
  }
  assert(prevBlockId > 0);

  // 将当前高度的块的chainID增一，若存在分叉的话. UNIQUE	(height, chain_id)
  // 当前即将插入的块的chainID必然为0
  {
    string sql = Strings::Format("UPDATE `0_blocks` SET `chain_id`=`chain_id`+1 "
                                 " WHERE `height` = %d ", height);
    db.update(sql.c_str());
  }

  // 构造插入SQL
  uint64_t difficulty = 0;
  BitsToDifficulty(header.nBits, difficulty);
  const int64_t rewardBlock = GetBlockValue(height, 0);
  const int64_t rewardFees  = blk.vtx[0].GetValueOut() - rewardBlock;
  assert(rewardFees >= 0);
  string sql1 = Strings::Format("INSERT INTO `0_blocks` (`block_id`, `height`, `hash`,"
                                " `version`, `mrkl_root`, `timestamp`, `bits`, `nonce`,"
                                " `prev_block_id`, `prev_block_hash`, `next_block_id`, "
                                " `next_block_hash`, `chain_id`, `size`,"
                                " `difficulty`, `tx_count`, `reward_block`, `reward_fees`, "
                                " `created_at`) VALUES ("
                                // 1. `block_id`, `height`, `hash`, `version`, `mrkl_root`, `timestamp`
                                " %lld, %d, '%s', %d, '%s', %lld, "
                                // 2. `bits`, `nonce`, `prev_block_id`, `prev_block_hash`,
                                " %lld, %lld, %lld, '%s', "
                                // 3. `next_block_id`, `next_block_hash`, `chain_id`, `size`,
                                " 0, '', %d, %d, "
                                // 4. `difficulty`, `tx_count`, `reward_block`, `reward_fees`, `created_at`
                                "%llu, %d, %lld, %lld, '%s');",
                                // 1.
                                blockId, height,
                                header.GetHash().ToString().c_str(),
                                header.nVersion,
                                header.hashMerkleRoot.ToString().c_str(),
                                header.nTime,
                                // 2.
                                header.nBits, header.nNonce, prevBlockId, prevBlockHash.c_str(),
                                // 3.
                                0/* chainId */, blockBytes,
                                // 4.
                                difficulty, blk.vtx.size(), rewardBlock, rewardFees, date("%F %T").c_str());
  if (db.update(sql1) != 1) {
    LOG_ERROR("insert block fail, sql: %s", sql1.c_str());
    return false;
  }

  // 更新前向块信息, 高度一以后的块需要，创始块不需要
  if (height > 0) {
    string sql2 = Strings::Format("UPDATE `0_blocks` SET `next_block_id` = %lld, `next_block_hash` = '%s'"
                                  " WHERE `hash` = '%s' ",
                                  blockId, header.GetHash().ToString().c_str(),
                                  prevBlockHash.c_str());
    if (db.update(sql2) != 1) {
      LOG_ERROR("update block fail, sql: %s", sql2.c_str());
      return false;
    }
  }

  return true;
}

bool _insertBlockTxs(MySQLConnection &db, const CBlock &blk, const int64_t blockId,
                     std::map<uint256, int64_t> &hash2id) {
  const string tableName = Strings::Format("block_txs_%04d", blockId % 100);
  MySQLResult res;
  char **row = nullptr;

  // 检查是否已经存在记录，由于可能发生块链分支切换，或许已经存在
  string sql = Strings::Format("SELECT IFNULL(MAX(`position`), -1) FROM `%s` WHERE `block_id` = %lld ",
                               tableName.c_str(), blockId);
  db.query(sql, res);
  assert(res.numRows() == 1);
  row = res.nextRow();
  int32_t existMaxPosition = atoi(row[0]);
  if (existMaxPosition != -1) {
    LOG_WARN("block_txs already exist");
    // 数量不符合，异常
    if (existMaxPosition != (int32_t)blk.vtx.size() - 1) {
      LOG_ERROR("exist txs is not equal block.vtx, now position is %d, should be %d",
                existMaxPosition, (int32_t)blk.vtx.size() - 1);
      return false;
    }
  }

  // 不存在，则插入新的记录
  // INSERT INTO `block_txs_0000` (`block_id`, `position`, `tx_id`, `created_at`)
  //    VALUES ('', '', '', '');
  vector<string> values;
  const string now = date("%F %T");
  const string fields = "`block_id`, `position`, `tx_id`, `created_at`";
  int i = 0;
  for (auto & it : blk.vtx) {
    values.push_back(Strings::Format("%lld,%d,%lld,'%s'",
                                     blockId, i++, hash2id[it.GetHash()],
                                     now.c_str()));
  }
  if (multiInsert(db, tableName, fields, values)) {
    return true;
  }

  return false;
}

// 接收一个新块
bool Parser::acceptBlock(const uint256 &blkhash, const int height) {
  // 获取块Raw Hex
  string blkRawHex;
  int32_t chainId;
  int64_t blockId;
  if (!getBlockRawHexByHash(blkhash, dbExplorer_, &blkRawHex, &chainId, &blockId)) {
    LOG_ERROR("can't find block by hash: %s", blkhash.ToString().c_str());
    return false;
  }
  assert(chainId == 0);

  // 解码Raw Hex
  vector<unsigned char> blockData(ParseHex(blkRawHex));
  CDataStream ssBlock(blockData, SER_NETWORK, BITCOIN_PROTOCOL_VERSION);
  CBlock blk;
  try {
    ssBlock >> blk;
  }
  catch (std::exception &e) {
    LOG_ERROR("Block decode failed, hash: %s", blkhash.ToString().c_str());
    return false;
  }

  // 拿到 tx_hash -> tx_id 的对应关系
  std::set<uint256> hashVec;
  std::map<uint256, int64_t> hash2id;
  for (auto & it : blk.vtx) {
    hashVec.insert(it.GetHash());
  }
  txsHash2ids(hashVec, hash2id);

  // 插入数据至 table.0_blocks
  if (!_insertBlock(dbExplorer_, blk, blockId, height, (int32_t)blkRawHex.length()/2)) {
    return false;
  }

  // 插入数据至 table.block_txs_xxxx
  if (!_insertBlockTxs(dbExplorer_, blk, blockId, hash2id)) {
    return false;
  }

  return false;
}

// 接收一个新的交易
bool Parser::acceptTx(const uint256 &txHash) {
  // TODO
}

// 获取上次txlog的进度偏移量
int64_t Parser::getLastTxLogOffset() {
  MySQLResult res;
  int64_t lastTxLogOffset = 0;
  char **row = nullptr;
  string sql;

  // find last tx log ID
  sql = "SELECT `value` FROM `0_explorer_meta` WHERE `key`='jiexi.last_txlog_offset'";
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

