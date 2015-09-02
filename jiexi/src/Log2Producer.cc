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

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/file.h>

#include <boost/filesystem.hpp>

namespace fs = boost::filesystem;

//////////////////////////////  MemTxRepository  ///////////////////////////////
MemTxRepository::MemTxRepository() {}
MemTxRepository::~MemTxRepository() {}

bool MemTxRepository::addTx(const CTransaction &tx,
                            vector<uint256> &conflictTxs) {
  assert(tx.IsCoinBase() == false);
  const uint256 txhash = tx.GetHash();

  if (isExist(txhash)) {
    LOG_WARN("already in MemRepo: %s", txhash.ToString().c_str());
    return false;  // already exist
  }

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
    // 标记内存中已经花费的output
    for (auto &it : tx.vin) {
      TxOutputKey out(it.prevout.hash, it.prevout.n);
      spentOutputs_.insert(make_pair(out, txhash));
    }
    txs_[txhash] = tx;
    unSyncTxsInsert_.insert(txhash);

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

void MemTxRepository::removeTxs(const vector<uint256> &txhashs,
                                const bool ingoreEmpty) {
  for (auto &hash : txhashs) {
    const bool exist = (txs_.find(hash) != txs_.end());
    if (!exist) {
      if (!ingoreEmpty) {
      	THROW_EXCEPTION_DBEX("tx not in memrepo: %s", hash.ToString().c_str());
      }
      continue;
    }

    CTransaction &tx = txs_[hash];
    for (auto &it : tx.vin) {
      TxOutputKey out(it.prevout.hash, it.prevout.n);
      auto it2 = spentOutputs_.find(out);
      assert(it2 != spentOutputs_.end());
      spentOutputs_.erase(it2);
    }

    txs_.erase(hash);
    unSyncTxsDelete_.insert(hash);
  }
}

void _getMemrepoTxsDeleteSQL(const vector<string> &arr, string &sql) {
  sql = "DELETE FROM `0_memrepo_txs` WHERE `tx_hash` IN ('";
  sql += implode(arr, "','");
  sql += "')";
}

//
// 将内存中的交易变化同步至DB，该函数必须在DB事务中执行
// WARNING: 若单个块交易量非常大，可能造成Mysql单个事务容纳不下这么多SQL，当恰巧崩溃时会导致
//          log2producer 无法从 0_memrepo_txs 恢复出交易
//
void MemTxRepository::syncToDB(MySQLConnection &db) {
  string sql;

  //
  // 插入交易
  //
  if (unSyncTxsInsert_.size()) {
    vector<string> values;
    for (auto it : unSyncTxsInsert_) {
      const string hashStr = it.ToString();
      sql = Strings::Format("'%s','%s'", hashStr.c_str(), date("%F %T").c_str());
      values.push_back(sql);
    }
    multiInsert(db, "0_memrepo_txs", "`tx_hash`, `created_at`", values);
    LOG_DEBUG("memrepo sync to DB, insert txs: %llu", unSyncTxsInsert_.size());
    unSyncTxsInsert_.clear();
  }

  //
  // 删除交易
  //
  if (unSyncTxsDelete_.size()) {
    vector<string> hashVec;
    for (auto it : unSyncTxsDelete_) {
      hashVec.push_back(it.ToString());
      if (hashVec.size() > 5000) {  // 批量执行
        _getMemrepoTxsDeleteSQL(hashVec, sql);
        db.updateOrThrowEx(sql, (int32_t)hashVec.size());
        hashVec.clear();
      }
    }
    if (hashVec.size() > 0) {
      _getMemrepoTxsDeleteSQL(hashVec, sql);
      db.updateOrThrowEx(sql, (int32_t)hashVec.size());
      hashVec.clear();
    }
    assert(hashVec.size() == 0);
    LOG_DEBUG("memrepo sync to DB, delete txs: %llu", unSyncTxsDelete_.size());
    unSyncTxsDelete_.clear();
  }
}

void MemTxRepository::ignoreUnsyncData() {
  unSyncTxsInsert_.clear();
  unSyncTxsDelete_.clear();
}

size_t MemTxRepository::size() const {
  return txs_.size();
}

bool MemTxRepository::isExist(const uint256 &txhash) const {
  return txs_.find(txhash) != txs_.end();
}

///////////////////////////////  Log2Producer  /////////////////////////////////
Log2Producer::Log2Producer(): running_(false), db_(Config::GConfig.get("mysql.uri")) {
  log1Dir_ = Config::GConfig.get("log1.dir");
}

Log2Producer::~Log2Producer() {
}

static void _initLog2_loadMemrepoTxs(MySQLConnection &db,
                                         MemTxRepository &memRepo) {
  string sql = "SELECT `tx_hash` FROM `0_memrepo_txs` ORDER BY `position`";
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
    LOG_DEBUG("load unconfirmed tx: %s", hash.ToString().c_str());
  }

  LOG_INFO("load unconfirmed txs: %llu", memRepo.size());

  memRepo.ignoreUnsyncData();  // 忽略一下载入的数据，已经同步过了
}

void Log2Producer::initLog2() {
  LogScope ls("Log2Producer::initLog2()");
  string sql;
  MySQLResult res;
  char **row;

  //
  // 清理临时的记录（未完整状态的）
  //
  {
    sql = "DELETE FROM `0_txlogs2` WHERE `batch_id` = -1";
    db_.update(sql);
  }

  //
  // 找最后一个块记录，即最后一条 block_id 非零的记录
  //
  int64_t blockId = 0;
  {
    sql = "SELECT `block_id` FROM `0_txlogs2` WHERE `block_id`>0 ORDER BY `id` DESC LIMIT 1";
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
  if (log2BlockHash_ == uint256() || log2BlockHeight_ < 0) {
    THROW_EXCEPTION_DBEX("invalid log2 latest block: %d, %s",
                         log2BlockHeight_, log2BlockHash_.ToString().c_str());
  }
  LOG_INFO("log2 latest block, height: %d, hash: %s",
           log2BlockHeight_, log2BlockHash_.ToString().c_str());

  //
  // 载入内存库中(未确认)交易
  //
  _initLog2_loadMemrepoTxs(db_, memRepo_);
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
      break;
    }
  }
  if (lines.size() > 0) {
    LOG_DEBUG("load log1 items: %lld", lines.size());
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

void Log2Producer::checkEnvironment() {
  // 检测 innodb_log_file_size
  {
    // 不得低于32MB
    // 假设平均sql语句是256字节，那么32M对应的SQL为：125,000条，目前单个块的交易数量远远低于此值
    // 阿里云的RDS，目前该值是 1048576000 (1G)
    // TODO: 随着块增长，需提高 innodb_log_file_size 的最小限制
    const int64_t size = atoi64(db_.getVariable("innodb_log_file_size").c_str());
    const int64_t minSize  = 32  * 1000 * 1000;
    const int64_t recoSize = 256 * 1000 * 1000;
    if (size < minSize) {
      THROW_EXCEPTION_DBEX("mysql innodb_log_file_size(%lld) is less than min size: %lld",
                           size, minSize);
    }
    if (size < recoSize) {
      LOG_WARN("mysql innodb_log_file_size(%lld) is less than recommended size: %lld",
               size, recoSize);
    }
  }

  // max_allowed_packet
  {
    const int64_t size = atoi64(db_.getVariable("max_allowed_packet").c_str());
    const int64_t minSize  = 32 * 1000 * 1000;
    const int64_t recoSize = 64 * 1000 * 1000;
    if (size < minSize) {
      THROW_EXCEPTION_DBEX("mysql max_allowed_packet(%lld) is less than min size: %lld",
                           size, minSize);
    }
    if (size < recoSize) {
      LOG_WARN("mysql max_allowed_packet(%lld) is less than recommended size: %lld",
               size, recoSize);
    }
  }
}

void Log2Producer::init() {
  running_ = true;

  checkEnvironment();
  initLog2();
  syncLog1();
}

void Log2Producer::stop() {
  LOG_INFO("stop log2producer...");
  running_ = false;
}

void Log2Producer::handleTx(Log1 &log1Item) {
  //
  // 接收新的交易
  //
  string sql;
  const CTransaction &tx = log1Item.getTx();
  const uint256 hash = tx.GetHash();
  LOG_INFO("process tx(+): %s", hash.ToString().c_str());

  vector<uint256> conflictTxs;
  const string nowStr = date("%F %T");
  const bool res = memRepo_.addTx(tx, conflictTxs);

  // 冲突的交易
  if (res == false) {
    LOG_WARN("reject tx: %s", hash.ToString().c_str());
    for (auto &it : conflictTxs) {
      LOG_WARN("\tconflict tx: %s", it.ToString().c_str());
    }
    return;
  }

  // 插入row txs
  insertRawTx(db_, tx);

  // 无冲突，插入DB
  sql = Strings::Format("INSERT INTO `0_txlogs2` (`batch_id`, `type`, `block_height`, "
                        " `block_id`,`block_timestamp`,`tx_hash`,`created_at`,`updated_at`) "
                        " VALUES ("
                        " (SELECT IFNULL(MAX(`batch_id`), 0) + 1 FROM `0_txlogs2` as t1), "
                        " %d, -1, -1, 0, '%s', '%s', '%s');",
                        LOG2TYPE_TX_ACCEPT,
                        hash.ToString().c_str(),
                        nowStr.c_str(), nowStr.c_str());

  db_.execute("START TRANSACTION");
  db_.updateOrThrowEx(sql, 1);
  memRepo_.syncToDB(db_);
  db_.execute("COMMIT");
}

void Log2Producer::handleBlockAccept(Log1 &log1Item) {
  //
  // 块高度前进
  //
  const CBlock &blk  = log1Item.getBlock();
  const uint256 hash = blk.GetHash();
  const string lsStr = Strings::Format("process block(+): %d, %s",
                                       log1Item.blockHeight_,
                                       hash.ToString().c_str());
  LogScope ls(lsStr.c_str());

  // 1.0 过一遍内存，通过添加至 memRepo 找到冲突的交易
  vector<uint256> txHashs;
  set<uint256> alreadyInMemTxHashs;
  vector<uint256> conflictTxs;

  for (auto &tx : blk.vtx) {
    if (tx.IsCoinBase()) { continue; }  // coinbase tx 无需进内存池
    const uint256 txhash = tx.GetHash();
    txHashs.push_back(txhash);

    if (memRepo_.isExist(txhash)) {
      alreadyInMemTxHashs.insert(txhash);
    } else {
      memRepo_.addTx(tx, conflictTxs);
    }
  }

  // 1.1 移除冲突交易
  memRepo_.removeTxs(conflictTxs);

  // 1.2 移除块的交易，忽略不存在的交易（有可能因为冲突没有添加至 memRepo 或 coinbase tx）
  memRepo_.removeTxs(txHashs, true/* ingore not exist tx */);

  // 2.0 插入 raw_blocks
  const int64_t blockId = insertRawBlock(db_, blk, log1Item.blockHeight_);

  // 2.1 插入 raw_txs
  for (auto &tx : blk.vtx) {
    insertRawTx(db_, tx);
  }

  // 3.0 批量插入数据
  vector<string> values;
  const string nowStr = date("%F %T");
  const string fields = "`batch_id`, `type`, `block_height`, `block_id`, "
  "`block_timestamp`, `tx_hash`, `created_at`, `updated_at`";

  // 冲突的交易，需要做拒绝处理
  for (auto &it : conflictTxs) {
    string item = Strings::Format("-1,%d,-1,-1,0,'%s','%s','%s'",
                                  LOG2TYPE_TX_REJECT,
                                  it.ToString().c_str(),
                                  nowStr.c_str(), nowStr.c_str());
    values.push_back(item);
  }

  // 新块的交易，做确认操作
  for (auto &tx : blk.vtx) {
    string item;
    const uint256 txhash = tx.GetHash();
    const int64_t blockTimestamp = blk.GetBlockTime();

    // 首次处理的，需要补 accept 操作
    if (alreadyInMemTxHashs.find(txhash) == alreadyInMemTxHashs.end()) {
      item = Strings::Format("-1,%d,-1,-1,%lld,'%s','%s','%s'",
                             LOG2TYPE_TX_ACCEPT, blockTimestamp,
                             txhash.ToString().c_str(),
                             nowStr.c_str(), nowStr.c_str());
      values.push_back(item);
    }

    // confirm
    item = Strings::Format("-1,%d,%d,%lld,%lld,'%s','%s','%s'",
                           LOG2TYPE_TX_CONFIRM,
                           log1Item.blockHeight_, blockId, blockTimestamp,
                           txhash.ToString().c_str(),
                           nowStr.c_str(), nowStr.c_str());
    values.push_back(item);
  }

  // 插入本批次数据
  multiInsert(db_, "0_txlogs2", fields, values);

  // 提交本批数据
  commitBatch(values.size());

  LOG_INFO("block txs: %llu, conflict txs: %llu", blk.vtx.size(), conflictTxs.size());
}

void Log2Producer::commitBatch(const size_t expectAffectedRows) {
  string sql;
  MySQLResult res;
  char **row;

  // fetch next batch_id
  sql = "SELECT IFNULL(MAX(`batch_id`), 0) + 1 FROM `0_txlogs2`";
  db_.query(sql, res);
  row = res.nextRow();
  const int64_t nextBatchID = atoi64(row[0]);

  // update batch_id
  sql = Strings::Format("UPDATE `0_txlogs2` SET `batch_id`=%lld WHERE `batch_id`=-1",
                        nextBatchID);
  //
  // 使用事务提交，保证更新成功的数据就是既定的数量。有差错则异常，DB事务无法提交。
  //
  db_.execute("START TRANSACTION");
  db_.updateOrThrowEx(sql, (int32_t)expectAffectedRows);
  memRepo_.syncToDB(db_);
  db_.execute("COMMIT");
}

void Log2Producer::handleBlockRollback(Log1 &log1Item) {
  //
  // 块高度后退
  //
  const CBlock &blk  = log1Item.getBlock();
  const uint256 hash = blk.GetHash();
  const string lsStr = Strings::Format("process block(-): %d, %s",
                                       log1Item.blockHeight_,
                                       hash.ToString().c_str());
  LogScope ls(lsStr.c_str());

  //
  // 交易重新添加到内存池里
  //
  vector<uint256> conflictTxs;
  for (auto &tx : blk.vtx) {
    if (tx.IsCoinBase()) { continue; }

    // 应该是不存在，且没有冲突交易的
    if (!memRepo_.addTx(tx, conflictTxs)) {
      LOG_INFO("unconfirm tx: %s", tx.GetHash().ToString().c_str());
      for (auto &it : conflictTxs) {
        LOG_WARN("\tconflict tx: %s", it.ToString().c_str());
      }
      THROW_EXCEPTION_DBEX("thare are conflict txs, should not happened");
    }
  }

  //
  // 添加反确认
  //
  vector<string> values;
  const string nowStr = date("%F %T");
  const string fields = "`batch_id`, `type`, `block_height`, `block_id`,"
  "`block_timestamp`,`tx_hash`, `created_at`, `updated_at`";

  // get block ID
  const int64_t blockId = insertRawBlock(db_, blk, log1Item.blockHeight_);

  // 新块的交易，做反确认操作
  for (auto &tx : blk.vtx) {
    string item = Strings::Format("-1,%d,%d,%lld,%lld,'%s','%s','%s'",
                                  LOG2TYPE_TX_UNCONFIRM,
                                  log1Item.blockHeight_, blockId,
                                  blk.GetBlockTime(),
                                  tx.GetHash().ToString().c_str(),
                                  nowStr.c_str(), nowStr.c_str());
    values.push_back(item);
  }

  // 插入本批次数据
  multiInsert(db_, "0_txlogs2", fields, values);

  // 提交本批数据
  commitBatch(values.size());

  LOG_INFO("block txs: %llu, conflict txs: %llu", blk.vtx.size(), conflictTxs.size());
}

void Log2Producer::handleBlock(Log1 &log1Item) {
  //
  // 块高度前进
  //
  if (log1Item.blockHeight_ > log2BlockHeight_) {
    handleBlockAccept(log1Item);
  }
  //
  // 块高度后退
  //
  else if (log1Item.blockHeight_ < log2BlockHeight_) {
    handleBlockRollback(log1Item);
  }
  else {
    THROW_EXCEPTION_DBEX("block are same height, log1: %s",
                         log1Item.toString().c_str());
  }
}

void Log2Producer::run() {
  LogScope ls("Log2Producer::run()");

  while (running_) {
    vector<string> lines;
    tryReadLog1(lines);

    if (!running_) { break; }
    if (lines.size() == 0) {
      sleepMs(200);
      continue;
      // TODO: 可改进为监听文件事件代替sleep操作，减少轮询且提高效率
    }

    for (const auto &line : lines) {
      Log1 log1Item;
      log1Item.parse(line);

      // Tx
      if (log1Item.isTx()) {
        handleTx(log1Item);
      }
      // Block
      else if (log1Item.isBlock()) {
        handleBlock(log1Item);
      }
      else {
        THROW_EXCEPTION_DBEX("invalid log1 type, log line: %s", line.c_str());
      }
    } /* /for */
  } /* /while */
}

