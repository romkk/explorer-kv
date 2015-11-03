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

bool MemTxRepository::addTx(const CTransaction &tx) {
  assert(tx.IsCoinBase() == false);
  const uint256 txhash = tx.GetHash();

  if (isExist(txhash)) {
    LOG_WARN("already in MemRepo: %s", txhash.ToString().c_str());
    return false;  // already exist
  }

  //
  // 判断是否有冲突交易
  //
  for (auto &it : tx.vin) {
    TxOutputKey out(it.prevout.hash, it.prevout.n);
    // 新交易的输出已经在已花费列表中
    if (spentOutputs_.find(out) != spentOutputs_.end()) {
      LOG_WARN("conflict tx: %s, input: %s:%d, already spent in tx: %s",
               txhash.ToString().c_str(),
               it.prevout.hash.ToString().c_str(), it.prevout.n,
               spentOutputs_[out].ToString().c_str());
      return false;
    }
  }

  //
  // 没有冲突，加入到内存
  //
  for (auto &it : tx.vin) {
    // 标记内存中已经花费的output
    TxOutputKey out(it.prevout.hash, it.prevout.n);
    spentOutputs_.insert(make_pair(out, txhash));
  }
  txs_[txhash] = tx;
  unSyncTxsInsert_.insert(txhash);

  return true;
}

//
// 获取冲突交易Hash
//
uint256 MemTxRepository::getConflictTx(const CTransaction &tx) {
  //
  // 遍历该交易输入，找出花费这些交易链上的叶子交易
  //
  for (auto &it : tx.vin) {
    TxOutputKey out(it.prevout.hash, it.prevout.n);
    if (spentOutputs_.find(out) == spentOutputs_.end()) {
      continue;
    }

    // 当为交易自身时，说明异常
    const uint256 spentTxHash = spentOutputs_[out];
    if (spentTxHash == tx.GetHash()) {
      THROW_EXCEPTION_DBEX("spentTxHash is equal to tx.GetHash(): %s",
                           spentTxHash.ToString().c_str());
    }

    // 获取已经花费的交易链的末端交易
    return getSpentEndTx(txs_[spentTxHash]);
  }

  return tx.GetHash();  // return self, should not arrive here
}

//
// 获取已经花费的交易链的末端交易（未被花费的交易）
// 递归：由于可能是交易链，本函数返回交易链最深的一个交易(未被任何花费的交易)。
//
uint256 MemTxRepository::getSpentEndTx(const CTransaction &tx) {
  const uint256 txhash = tx.GetHash();
  LOG_DEBUG("[MemTxRepository::getSpentEndTx] txhash: %s", txhash.ToString().c_str());

  // 遍历交易的输出，检查交易输出流向
  for (int32_t i = 0; i < tx.vout.size(); i++) {
    TxOutputKey out(txhash, i);
    if (spentOutputs_.find(out) == spentOutputs_.end()) {
      continue;
    }

    // 递归执行，向下一层交易获取
    return getSpentEndTx(txs_[spentOutputs_[out]]);
  }

  return tx.GetHash();
}

void MemTxRepository::removeTx(const uint256 &hash) {
  LOG_DEBUG("memrepo remove tx: %s", hash.ToString().c_str());
  
  const bool exist = (txs_.find(hash) != txs_.end());
  if (!exist) {
    THROW_EXCEPTION_DBEX("tx not in memrepo: %s", hash.ToString().c_str());
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

void MemTxRepository::removeTxs(const vector<uint256> &txhashs) {
  for (auto &hash : txhashs) {
    removeTx(hash);
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



///////////////////////////////  BlockTimestamp  /////////////////////////////////
BlockTimestamp::BlockTimestamp(const int32_t limit): limit_(limit),currMax_(0) {
}

int64_t BlockTimestamp::getMaxTimestamp() const {
  return currMax_;
}

void BlockTimestamp::pushBlock(const int32_t height, const int64_t ts) {
  assert(blkTimestamps_.find(height) == blkTimestamps_.end());
  blkTimestamps_[height] = ts;
  if (ts > currMax_) {
    currMax_ = ts;
  } else {
    LOG_WARN("block %lld timestamp(%lld) is less than curr max: %lld",
             height, ts, currMax_);
  }

  // 检查数量限制，超出后移除首个元素
  while (blkTimestamps_.size() > limit_) {
    blkTimestamps_.erase(blkTimestamps_.begin());
  }
}

void BlockTimestamp::popBlock() {
  assert(blkTimestamps_.size() > 0);
  // map 尾部的 key 最大，也意味着块最高
  blkTimestamps_.erase(std::prev(blkTimestamps_.end()));

  currMax_ = 0;
  for (auto it : blkTimestamps_) {
    if (currMax_ < it.second) {
      currMax_ = it.second;
    }
  }
}


///////////////////////////////  Log2Producer  /////////////////////////////////
Log2Producer::Log2Producer(): running_(false),
db_(Config::GConfig.get("mysql.uri")), blkTs_(2016),
log2BlockBeginHeight_(-1), currBlockHeight_(-1)
{
  kTableTxlogs2Fields_ = "`batch_id`, `type`, `block_height`, \
  `block_id`, `max_block_timestamp`, `tx_hash`, `created_at`, `updated_at`";

  log1Dir_ = Config::GConfig.get("log1.dir");
  notifyFileTParser_      = log1Dir_ + "/NOTIFY_LOG2_TO_TPARSER";
  notifyFileLog1Producer_ = log1Dir_ + "/NOTIFY_LOG1_TO_LOG2";

  // 创建通知文件, 通知 tparser 的
  {
    FILE *f = fopen(notifyFileTParser_.c_str(), "w");
    if (f == nullptr) {
      THROW_EXCEPTION_DBEX("create file fail: %s", notifyFileTParser_.c_str());
    }
    fclose(f);
  }
  watchNotifyThread_ = thread(&Log2Producer::threadWatchNotifyFile, this);
}

Log2Producer::~Log2Producer() {
  changed_.notify_all();
  if (watchNotifyThread_.joinable()) {
    watchNotifyThread_.join();
  }
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

    CTransaction tx;
    if (!DecodeHexTx(tx, txHex)) {
      THROW_EXCEPTION_DBEX("decode tx failure, hex: %s", txHex.c_str());
    }
    if (memRepo.addTx(tx) == false) {
      THROW_EXCEPTION_DBEX("add tx to mem repo fail, tx: %s", hash.ToString().c_str());
    }
    LOG_DEBUG("load 0_memrepo_txs tx: %s", hash.ToString().c_str());
  }

  LOG_INFO("load 0_memrepo_txs txs: %llu", memRepo.size());

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
  // 必须 tpaser 消费跟进到最近的 txlogs2
  // table.0_blocks 的最新块高度就是当前链的高度，前提就是 tparser 必须消费到最新的 txlogs2
  //
  int64_t jiexiLastTxlog2Offset = 0;
  int64_t lastTxlogs2Id = 0;
  {
    sql = "SELECT `value` FROM `0_explorer_meta` WHERE `key`='jiexi.last_txlog2_offset'";
    db_.query(sql, res);
    if (res.numRows() != 0) {
      row = res.nextRow();
      jiexiLastTxlog2Offset = atoi64(row[0]);
    }

    sql = "SELECT `id` FROM `0_txlogs2` ORDER BY `id` DESC LIMIT 1";
    db_.query(sql, res);
    if (res.numRows() != 0) {
      row = res.nextRow();
      lastTxlogs2Id = atoi64(row[0]);
    }
    if (jiexiLastTxlog2Offset != lastTxlogs2Id) {
      THROW_EXCEPTION_DBEX("jiexi.last_txlog2_offset(%lld) is NOT match table.0_txlogs2's max id(%lld), "
                           "please wait util 'tparser' catch up latest txlogs2",
                           jiexiLastTxlog2Offset, lastTxlogs2Id);
    }
  }

  if (lastTxlogs2Id > 0) {
    // 找最后一个块记录：当前链的最大高度块
    sql = "SELECT `hash`,`height` FROM `0_blocks` WHERE `chain_id`=0 ORDER BY `height` DESC LIMIT 1";
    db_.query(sql, res);
    assert(res.numRows() != 0);
    row = res.nextRow();
    log2BlockBeginHash_   = uint256(row[0]);
    log2BlockBeginHeight_ = atoi(row[1]);
  }
  else {
    // 数据库没有记录，则以配置文件的块信息作为起始块
    log2BlockBeginHash_   = uint256(Config::GConfig.get("log2.begin.block.hash"));
    log2BlockBeginHeight_ = (int32_t)Config::GConfig.getInt("log2.begin.block.height");
  }

  if (log2BlockBeginHash_ == uint256() || log2BlockBeginHeight_ < 0) {
    THROW_EXCEPTION_DBEX("invalid log2 latest block: %d, %s",
                         log2BlockBeginHeight_, log2BlockBeginHash_.ToString().c_str());
  }
  LOG_INFO("log2 latest block, height: %d, hash: %s",
           log2BlockBeginHeight_, log2BlockBeginHash_.ToString().c_str());

  //
  // 载入内存库中(未确认)交易
  //
  _initLog2_loadMemrepoTxs(db_, memRepo_);

  //
  // 获取当前高度之前的块的最大时间戳
  //
  {
    // 必须 tpaser 消费跟进到最近的 txlogs2，才能保证能从 table.0_blocks 查询到最新
    sql = Strings::Format("SELECT `hash` FROM `0_blocks` WHERE "
                          " `height`=%d AND `chain_id`=0 AND `hash`='%s' ",
                          log2BlockBeginHeight_, log2BlockBeginHash_.ToString().c_str());
    db_.query(sql, res);
    if (res.numRows() == 0) {
      THROW_EXCEPTION_DBEX("can't find block from table.0_blocks, %d : %s, "
                           "please wait util 'tparser' catch up latest txlogs2",
                           log2BlockBeginHeight_, log2BlockBeginHash_.ToString().c_str());
    }

    // 获取最近 2016 个块的时间戳
    sql = Strings::Format("SELECT * FROM (SELECT `timestamp`,`height` FROM `0_blocks`"
                          " WHERE `height` <= %d AND `chain_id` = 0 "
                          " ORDER BY `height` DESC LIMIT 2016) AS `t1` ORDER BY `height` ASC ",
                          log2BlockBeginHeight_);
    db_.query(sql, res);
    if (res.numRows() == 0) {
      THROW_EXCEPTION_DBEX("can't find max block timestamp, log2BlockHeight: %d",
                           log2BlockBeginHeight_);
    }
    for (int32_t i = (int32_t)res.numRows(); i > 0 ; i--) {
      row = res.nextRow();
      const int32_t height = atoi(row[1]);
      blkTs_.pushBlock(height, atoi64(row[0]));
      assert(height == log2BlockBeginHeight_ - i + 1);
    }
    LOG_INFO("found max block timestamp: %lld", blkTs_.getMaxTimestamp());
  }
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
      if (log1Item.blockHeight_         != log2BlockBeginHeight_ ||
          log1Item.getBlock().GetHash() != log2BlockBeginHash_) {
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
    THROW_EXCEPTION_DBEX("sync log1 failure");
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
    if (log1Ifstream.eof()) {
      // eof 表示没有遇到 \n 就抵达文件尾部了，通常意味着未完全读取一行
      // 读取完最后一行后，再读取一次，才会导致 eof() 为 true
      break;
    }
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

  currBlockHeight_ = log2BlockBeginHeight_;
  currBlockHash_   = log2BlockBeginHash_;
}

void Log2Producer::stop() {
  LOG_INFO("stop log2producer...");
  running_ = false;

  inotify_.RemoveAll();
  changed_.notify_all();
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
  const bool res = memRepo_.addTx(tx);

  // 冲突的交易
  if (res == false) {
    LOG_WARN("reject tx: %s", hash.ToString().c_str());
    return;
  }

  // 插入row txs
  insertRawTx(db_, tx);

  // 无冲突，插入DB
  sql = Strings::Format("INSERT INTO `0_txlogs2` (`batch_id`, `type`, `block_height`, "
                        " `block_id`,`max_block_timestamp`,`tx_hash`,`created_at`,`updated_at`) "
                        " VALUES ("
                        " (SELECT IFNULL(MAX(`batch_id`), 0) + 1 FROM `0_txlogs2` as t1), "
                        " %d, -1, -1, %lld, '%s', '%s', '%s');",
                        LOG2TYPE_TX_ACCEPT,
                        blkTs_.getMaxTimestamp(),  // 设置为前面最大的块时间戳
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

  // 先加入到块时间戳里，重新计算时间戳. 回滚块的时候是最后再 pop 块
  blkTs_.pushBlock(log1Item.blockHeight_, blk.GetBlockTime());

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
      //
      // 添加至内存池，目的：判断没有没有潜在冲突的交易。
      // 有冲突，每次移除掉叶子节点一个，直至不再冲突
      //
      while (memRepo_.addTx(tx) == false) {
        uint256 conflictTx = memRepo_.getConflictTx(tx);
        assert(conflictTx != tx.GetHash());
        memRepo_.removeTx(conflictTx);
        conflictTxs.push_back(conflictTx);
      }
    }
  }

  // 采用每次移除一个冲突交易树中的叶子节点的方法，应该不会重复移除某个交易
  if (conflictTxs.size()) {
    set<uint256> conflictTxsSet(conflictTxs.begin(), conflictTxs.end());
    assert(conflictTxsSet.size() == conflictTxs.size());
  }

  // 1.2 移除内存中块的交易，块中的交易应该都在内存中
  memRepo_.removeTxs(txHashs);

  // 2.0 插入 raw_blocks
  const int64_t blockId = insertRawBlock(db_, blk, log1Item.blockHeight_);

  // 2.1 插入 raw_txs
  // 这里保证了，能够查询到 raw_block 时，则其中的所有 raw_tx 都能查询到
  for (auto &tx : blk.vtx) {
    insertRawTx(db_, tx);
  }

  // 3.0 批量插入数据
  vector<string> values;
  const string nowStr = date("%F %T");

  // 冲突的交易，需要做拒绝处理，注意顺序
  // conflictTxs 中其实是逆序，因为前面是先剔除冲突交易树的叶子节点
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

    // 首次处理的，需要补 accept 操作
    if (alreadyInMemTxHashs.find(txhash) == alreadyInMemTxHashs.end()) {
      item = Strings::Format("-1,%d,-1,-1,%lld,'%s','%s','%s'",
                             LOG2TYPE_TX_ACCEPT, blkTs_.getMaxTimestamp(),
                             txhash.ToString().c_str(),
                             nowStr.c_str(), nowStr.c_str());
      values.push_back(item);
    }

    // confirm
    item = Strings::Format("-1,%d,%d,%lld,%lld,'%s','%s','%s'",
                           LOG2TYPE_TX_CONFIRM,
                           log1Item.blockHeight_, blockId, blkTs_.getMaxTimestamp(),
                           txhash.ToString().c_str(),
                           nowStr.c_str(), nowStr.c_str());
    values.push_back(item);
  }

  // 插入本批次数据
  multiInsert(db_, "0_txlogs2", kTableTxlogs2Fields_, values);

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

// 清理txlogs2的内存交易
void Log2Producer::clearMempoolTxs() {
  string sql;
  MySQLResult res;
  char **row = nullptr;

  //
  // 由于 table.0_memrepo_txs 是有交易前后依赖顺序的，所以逆序读出，批量移除即可
  //
  sql = "SELECT `tx_hash` FROM `0_memrepo_txs` ORDER BY `position` DESC";
  db_.query(sql, res);
  if (res.numRows() == 0) {
    LOG_WARN("table.0_memrepo_txs is empty, clear mempool finish");
    return;
  }

  vector<string> values;
  const string nowStr = date("%F %T");
  while ((row = res.nextRow()) != nullptr) {
    const uint256 hash = uint256(row[0]);
    string item = Strings::Format("-1,%d,-1,-1,0,'%s','%s','%s'",
                                  LOG2TYPE_TX_REJECT,
                                  hash.ToString().c_str(),
                                  nowStr.c_str(), nowStr.c_str());
    values.push_back(item);
    memRepo_.removeTx(hash);
  }
  assert(memRepo_.size() == 0);

  // 插入本批次数据
  multiInsert(db_, "0_txlogs2", kTableTxlogs2Fields_, values);

  // 提交本批数据
  commitBatch(values.size());
}

void Log2Producer::handleBlockRollback(const int32_t height, const CBlock &blk) {
  //
  // 块高度后退
  //
  const uint256 hash = blk.GetHash();
  const string lsStr = Strings::Format("process block(-): %d, %s",
                                       height, hash.ToString().c_str());
  LogScope ls(lsStr.c_str());

  //
  // 交易重新添加到内存池里，反序遍历
  //
  for (auto tx = blk.vtx.rbegin(); tx != blk.vtx.rend(); ++tx) {
    if (tx->IsCoinBase()) { continue; }

    // 应该是不存在，且没有冲突交易的
    if (memRepo_.addTx(*tx) == false) {
      LOG_INFO("unconfirm tx add to memRepo fail: %s", tx->GetHash().ToString().c_str());
      THROW_EXCEPTION_DBEX("thare are conflict txs, should not happened");
    }
  }

  //
  // 添加反确认
  //
  vector<string> values;
  const string nowStr = date("%F %T");

  // get block ID
  const int64_t blockId = insertRawBlock(db_, blk, height);

  // 新块的交易，做反确认操作，反序遍历
  for (auto tx = blk.vtx.rbegin(); tx != blk.vtx.rend(); ++tx) {
    string item = Strings::Format("-1,%d,%d,%lld,%lld,'%s','%s','%s'",
                                  LOG2TYPE_TX_UNCONFIRM,
                                  height, blockId,
                                  blkTs_.getMaxTimestamp(),
                                  tx->GetHash().ToString().c_str(),
                                  nowStr.c_str(), nowStr.c_str());
    values.push_back(item);
  }

  // coinbase tx 需要 reject
  {
    string item = Strings::Format("-1,%d,%d,%lld,%lld,'%s','%s','%s'",
                                  LOG2TYPE_TX_REJECT,
                                  height, blockId,
                                  blkTs_.getMaxTimestamp(),
                                  blk.vtx[0].GetHash().ToString().c_str(),
                                  nowStr.c_str(), nowStr.c_str());
    values.push_back(item);
  }

  // 先做操作，再移除块时间戳
  blkTs_.popBlock();

  // 插入本批次数据
  multiInsert(db_, "0_txlogs2", kTableTxlogs2Fields_, values);

  // 提交本批数据
  commitBatch(values.size());

  LOG_INFO("block txs: %llu", blk.vtx.size());
}

void Log2Producer::_getBlockByHash(const uint256 &hash, CBlock &blk) {
  MySQLResult res;
  string sql;
  char **row;

  const string hashStr = hash.ToString();
  sql = Strings::Format("SELECT `hex` FROM `0_raw_blocks` WHERE `block_hash`='%s'",
                        hashStr.c_str());
  db_.query(sql, res);
  assert(res.numRows() == 1);
  row = res.nextRow();
  const string hex = string(row[0]);

  blk.SetNull();
  if (!DecodeHexBlk(blk, hex)) {
    THROW_EXCEPTION_DBEX("decode block fail, hash: %s, hex: %s",
                         hashStr.c_str(), hex.c_str());
  }
}

void Log2Producer::handleBlock(Log1 &log1Item) {
  //
  // 块高度前进
  //
  if (log1Item.blockHeight_ > currBlockHeight_) {
    assert(log1Item.blockHeight_ == currBlockHeight_ + 1);
    handleBlockAccept(log1Item);
  }
  //
  // 块高度后退
  //
  else if (log1Item.blockHeight_ < currBlockHeight_) {
    assert(log1Item.blockHeight_ + 1 == currBlockHeight_);
    //
    // 块后退时，后退的是上一个块，即当前块
    //
    CBlock currBlock;
    _getBlockByHash(currBlockHash_, currBlock);
    assert(currBlock.hashPrevBlock == log1Item.getBlock().GetHash());

    handleBlockRollback(currBlockHeight_, currBlock);
  }
  else {
    THROW_EXCEPTION_DBEX("block are same height, log1: %s",
                         log1Item.toString().c_str());
  }

  // 设置当前高度和哈希
  currBlockHeight_ = log1Item.blockHeight_;
  currBlockHash_   = log1Item.getBlock().GetHash();
}

void Log2Producer::doNotifyTParser() {
  //
  // 只读打开后就关闭掉，会产生一个通知事件，由 log2producer 捕获
  //     IN_CLOSE_NOWRITE: 一个以只读方式打开的文件或目录被关闭。
  //
  FILE *f = fopen(notifyFileTParser_.c_str(), "r");
  assert(f != nullptr);
  fclose(f);
}

void Log2Producer::threadWatchNotifyFile() {
  try {
    //
    // IN_CLOSE_NOWRITE :
    //     一个以只读方式打开的文件或目录被关闭
    //     A file or directory that had been open read-only was closed.
    // `cat FILE` 可触发该事件
    //
    InotifyWatch watch(notifyFileLog1Producer_, IN_CLOSE_NOWRITE);
    inotify_.Add(watch);
    LOG_INFO("watching notify file: %s", notifyFileLog1Producer_.c_str());

    while (running_) {
      inotify_.WaitForEvents();

      size_t count = inotify_.GetEventCount();
      while (count > 0) {
        InotifyEvent event;
        bool got_event = inotify_.GetEvent(&event);

        if (got_event) {
          string mask_str;
          event.DumpTypes(mask_str);
          LOG_DEBUG("get inotify event, mask: %s", mask_str.c_str());
        }
        count--;

        // notify other threads
        changed_.notify_all();
      }
    } /* /while */
  } catch (InotifyException &e) {
    THROW_EXCEPTION_DBEX("Inotify exception occured: %s", e.GetMessage().c_str());
  }
}

void Log2Producer::run() {
  LogScope ls("Log2Producer::run()");

  while (running_) {
    vector<string> lines;
    tryReadLog1(lines);

    if (!running_) { break; }

    if (lines.size() == 0) {
      UniqueLock ul(lock_);
      // 默认等待N毫秒，直至超时，中间有人触发，则立即continue读取记录
      changed_.wait_for(ul, chrono::milliseconds(10*1000));
      continue;
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

    // 通知
    doNotifyTParser();

  } /* /while */
}

