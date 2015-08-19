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

#include "Log2Producer.h"

namespace fs = boost::filesystem;

//////////////////////////////  MemTxRepository  ///////////////////////////////
bool MemTxRepository::addTx(const CTransaction &tx,
                            vector<uint256> &conflictTxs) {
  const uint256 txhash = tx.GetHash();

  // 返回第一层的冲突交易
  for (auto &it : tx.vin) {
    TxOutputKey out(it.prevout.hash, it.prevout.n);
    if (spentOutputs_.find(out) != spentOutputs_.end()) {
      conflictTxs.push_back(spentOutputs_[out]);
      LOG_WARN("conflict tx input: %s:%d, already spent in tx: %s",
               it.prevout.hash.ToString().c_str(), it.prevout.n,
               spentOutputs_[out].ToString().c_str());
    }
  }

  // 没有冲突，加入到内存
  if (conflictTxs.size() == 0) {
    assert(txs_.find(txhash) == txs_.end());

    // 标记内存中已经花费的output
    for (auto &it : tx.vin) {
      TxOutputKey out(it.prevout.hash, it.prevout.n);
      spentOutputs_.insert(make_pair(out, txhash));
    }
    txs_[txhash] = tx;

    return true;
  }

  // 有冲突，遍历冲突交易，迭代找出冲突交易的后续交易。深度优先的遍历方式。
  for (size_t i = 0; i < conflictTxs.size(); i++) {
    const uint256 chash = conflictTxs[i];

    // 该hash被谁花费了
    assert(txs_.find(chash) != txs_.end());
    for (int j = 0; j < txs_[chash].vout.size(); j++) {
      TxOutputKey out(chash, j);
      if (spentOutputs_.find(out) != spentOutputs_.end()) {
        conflictTxs.push_back(spentOutputs_[out]);
      }
    }
  }

  return false;
}

void MemTxRepository::removeTxs(const vector<uint256> &txhashs) {
  for (auto &hash : txhashs) {
    assert(txs_.find(hash) != txs_.end());
    CTransaction &tx = txs_[hash];

    for (auto &it : tx.vin) {
      TxOutputKey out(it.prevout.hash, it.prevout.n);
      auto it2 = spentOutputs_.find(out);
      assert(it2 != spentOutputs_.end());
      spentOutputs_.erase(it2);
    }
    txs_.erase(hash);
  }
}

size_t MemTxRepository::size() const {
  return txs_.size();
}


///////////////////////////////  Log2Producer  /////////////////////////////////
static void _initLog2_removeUnreadyLog2(MySQLConnection &db) {
  string sql;
  //
  // 清理临时的记录（未完整状态的），临时记录都在 txlogs2 最后，且连续的。
  // sql: 总是清理最后的 N 条记录(如果里面有 `is_ready` = 0的话)
  //
  sql = "DELETE FROM `txlogs2` WHERE ";
  sql += " `id` >= ((SELECT * FROM (SELECT MAX(`id`) FROM `txlogs2`) AS `t1`) - 1000)";
  sql += " AND `is_ready` = 0 ";

  uint64 delNum = 0;
  while ((delNum = db.update(sql)) > 0) {
    LOG_INFO("remove unready log2 records: %llu", delNum);
  };
}

static void _initLog2_loadUnconfirmedTxs(MySQLConnection &db,
                                         MemTxRepository &memRepo) {
  string sql = "SELECT `tx_hash` FROM `0_unconfirmed_txs` ORDER BY `position`";
  MySQLResult res;
  char **row = nullptr;
  db.query(sql, res);
  if (res.numRows() == 0)  {
    return;
  }

  while ((row = res.nextRow()) != nullptr) {
    const uint256 hash(row[0]);
    const string txHex = getTxHexByHash(db, hash);

    vector<uint256> conflictTxs;
    CTransaction tx;
    if (!DecodeHexTx(tx, txHex)) {
      THROW_EXCEPTION_DBEX("decode tx failure, hex: %s", txHex.c_str());
    }
    if (memRepo.addTx(tx, conflictTxs) == false) {
      THROW_EXCEPTION_DBEX("add tx to mem repo fail, tx: %s", hash.ToString().c_str());
    }
  }
}

void Log2Producer::initLog2() {
  LogScope ls("Log2Producer::initLog2()");
  string sql;
  MySQLResult res;
  char **row;

  //
  // 清理临时的记录（未完整状态的）
  //
  _initLog2_removeUnreadyLog2(db_);

  //
  // 找最后一个块记录，即最后一条 block_id 非零的记录
  //
  int64_t blockId = 0;
  {
    sql = "SELECT `block_id` FROM `txlogs2` WHERE `block_id`>0 ORDER BY `id` DESC LIMIT 1";
    db_.query(sql, res);
    if (res.numRows()) {
      row = res.nextRow();
      blockId = atoi64(row[0]);
    }
  }
  if (blockId > 0) {
    sql = Strings::Format("SELECT `block_hash`,`block_height` FROM `0_raw_blocks` WHERE `id` = %lld ",
                          blockId);
    db_.query(sql, res);
    if (res.numRows() == 0) {
      THROW_EXCEPTION_DBEX("can't find block by id: %lld", blockId);
    }
    row = res.nextRow();
    log2BlockHash_   = uint256(row[0]);
    log2BlockHeight_ = atoi(row[1]);
  } else {
    // 数据库没有记录，则以配置文件的块信息作为起始块
    log2BlockHash_   = uint256(Config::GConfig.get("log2.begin.block.hash"));
    log2BlockHeight_ = (int32_t)Config::GConfig.getInt("log2.begin.block.height");
  }
  LOG_INFO("log2 latest block, height: %d, hash: %s",
           log2BlockHeight_, log2BlockHash_.ToString().c_str());

  //
  // 载入未确认交易
  //
  _initLog2_loadUnconfirmedTxs(db_, memRepo_);
}

void Log2Producer::syncLog1() {
  LogScope ls("Log2Producer::syncLog1()");
  bool syncSuccess = false;

  //
  // 遍历 log1 所有文件，直至找到一样的块，若找到则同步完成
  //
  std::set<int32_t> filesIdxs;  // log1 所有文件
  fs::path filesPath(Strings::Format("%s/files", log1Dir_.c_str()));
  tryCreateDirectory(filesPath);
  for (fs::directory_iterator end, it(filesPath); it != end; ++it) {
    filesIdxs.insert(atoi(it->path().stem().c_str()));
  }

  // 反序遍历，从最新的文件开始找
  for (auto it = filesIdxs.rbegin(); it != filesIdxs.rend(); it++) {
    ifstream fin(Strings::Format("%s/files/%d.log", log1Dir_.c_str(), *it));
    string line;
    Log1 log1Item;
    while (getline(fin, line)) {
      log1Item.parse(line);
      if (log1Item.isTx()) { continue; }
      assert(log1Item.isBlock());
      if (log1Item.blockHeight_         != log2BlockHeight_ ||
          log1Item.getBlock().GetHash() != log2BlockHash_) {
        continue;
      }
      // 找到高度和哈希一致的块
      log1FileIndex_  = *it;
      log1FileOffset_ = fin.tellg();
      LOG_INFO("sync log1 success, file idx: %d, offset: %lld",
               log1FileIndex_, log1FileOffset_);
      syncSuccess = true;
      break;
    } /* /while */

    if (syncSuccess) { break; }
  } /* /for */

  if (!syncSuccess) {
    THROW_EXCEPTION_DBEX("sync log0 failure");
  }
}

void Log2Producer::tryRemoveOldLog1() {
  const int32_t keepLogNum = (int32_t)Config::GConfig.getInt("log1.files.max.num", 24 * 3);
  int32_t fileIdx = log1FileIndex_ - keepLogNum;

  // 遍历，删除所有小序列号的文件
  while (fileIdx >= 0) {
    const string file = Strings::Format("%s/files/%d.log",
                                        log1Dir_.c_str(), fileIdx--);
    if (!fs::exists(fs::path(file))) {
      break;
    }
    // try delete
    LOG_INFO("remove old log1: %s", file.c_str());
    if (!fs::remove(fs::path(file))) {
      THROW_EXCEPTION_DBEX("remove old log1 failure: %s", file.c_str());
    }
  }
}

void Log2Producer::tryReadLog1(vector<string> &lines) {
  const string currFile = Strings::Format("%s/files/%d.log",
                                          log1Dir_.c_str(), log1FileIndex_);
  const string nextFile = Strings::Format("%s/files/%d.log",
                                          log1Dir_.c_str(), log1FileIndex_ + 1);

  // 判断是否存在下一个文件，需要在读取当前文件之间判断，防止读取漏掉现有文件的最后内容
  const bool isNextExist = fs::exists(fs::path(nextFile));

  //
  // 打开文件并尝试读取新行
  //
  ifstream log1Ifstream(currFile);
  if (!log1Ifstream.is_open()) {
    THROW_EXCEPTION_DBEX("open file failure: %s", currFile.c_str());
  }
  log1Ifstream.seekg(log1FileOffset_);
  string line;
  while (getline(log1Ifstream, line)) {  // getline()读不到内容，则会关闭 ifstream
    lines.push_back(line);
    log1FileOffset_ = log1Ifstream.tellg();

    if (lines.size() > 500) {  // 每次最多处理500条日志
      LOG_WARN("reach max limit, stop load log1 items");
      break;
    }
  }
  if (lines.size() > 0) {
    return;
  }

  //
  // 探测新文件，仅当前面没有读取到新内容的时候
  //
  if (isNextExist == true && lines.size() == 0) {
    // 存在新的文件，切换索引，重置offset
    log1FileIndex_++;
    log1FileOffset_ = 0;
    LOG_INFO("swith log1 file, old: %s, new: %s ", currFile.c_str(), nextFile.c_str());

    tryRemoveOldLog1();
  }
}

