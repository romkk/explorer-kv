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
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/file.h>

#include <boost/filesystem.hpp>

#include "Log2Producer.h"
#include "KVDB.h"

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
    // 新交易的输入已经在已花费列表中，双花交易
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

// 移除某个交易，当有子交易时，则先移除子交易
void MemTxRepository::removeTxAndChildTx(const CTransaction &tx,
                                         vector<uint256> &removedHashes) {
  const uint256 txhash = tx.GetHash();
  while (1) {
    // 获取已经花费的交易链的末端交易，并移除之
    auto removeTxhash = getSpentEndTx(tx);
    removeTx(removeTxhash);
    removedHashes.push_back(removeTxhash);

    if (txhash == removeTxhash) {
      break;  // 移除的就是自己，则退出
    }
  }
}

// 返回移除的交易Hash
uint256 MemTxRepository::removeTx() {
  assert(size() > 0);

  auto lastItem = std::prev(txs_.end());
  auto txHash = getSpentEndTx(lastItem->second);
  removeTx(txHash);
  return txHash;
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
db_(Config::GConfig.get("mysql.uri")), blkTs_(2016*2), currBlockHeight_(-1), currLog1FileOffset_(-1)
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
  //
  // 数据库中的交易不会有双花冲突，这里载入时即使不按照顺序也是不会有问题的
  //
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

  // 打开 KVDB
  KVDB kvdb(Config::GConfig.get("rocksdb.path", ""));
  kvdb.open();

  int32_t log2BlockBeginHeight = -1;
  uint256 log2BlockBeginHash   = uint256();

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
    // 获取 tparser 最后消费记录ID
    string value;
    if (kvdb.getMayNotExist("90_tparser_txlogs_offset_id", value) == true /* exit */) {
      jiexiLastTxlog2Offset = atoi64(value);
    }

    sql = "SELECT `id` FROM `0_txlogs2` ORDER BY `id` DESC LIMIT 1";
    db_.query(sql, res);
    if (res.numRows() != 0) {
      row = res.nextRow();
      lastTxlogs2Id = atoi64(row[0]);
    }
    if (jiexiLastTxlog2Offset != lastTxlogs2Id) {
      THROW_EXCEPTION_DBEX("kvdb.90_tparser_txlogs_offset_id(%lld) is NOT match table.0_txlogs2's max id(%lld), "
                           "please wait util 'tparser' catch up latest txlogs2",
                           jiexiLastTxlog2Offset, lastTxlogs2Id);
    }
  }

  if (lastTxlogs2Id > 0) {
    vector<string> keys, values;
    // KVDB_PREFIX_BLOCK_HEIGHT
    kvdb.range("10_9999999999", "10_0000000000", 1, keys, values);
    log2BlockBeginHeight = atoi(keys[0].substr(3));
    log2BlockBeginHash   = uint256(values[0]);
  }
  else {
    // kvdb 没有记录，则以配置文件的块信息作为起始块
    log2BlockBeginHash   = uint256(Config::GConfig.get("log2.begin.block.hash"));
    log2BlockBeginHeight = (int32_t)Config::GConfig.getInt("log2.begin.block.height");
  }

  if (log2BlockBeginHash == uint256() || log2BlockBeginHeight < 0) {
    THROW_EXCEPTION_DBEX("invalid log2 latest block: %d, %s",
                         log2BlockBeginHeight, log2BlockBeginHash.ToString().c_str());
  }
  LOG_INFO("log2 latest block, height: %d, hash: %s",
           log2BlockBeginHeight, log2BlockBeginHash.ToString().c_str());

  //
  // 载入内存库中(未确认)交易
  //
  _initLog2_loadMemrepoTxs(db_, memRepo_);

  //
  // 获取当前高度之前的块的最大时间戳，构造出 blkTs_
  //
  {
    vector<string> keys, values;
    // KVDB_PREFIX_BLOCK_HEIGHT
    kvdb.range(Strings::Format("10_%010d", log2BlockBeginHeight - 2016*2),
               "10_9999999999", 10000/* limit，这里肯定不会达到limit */, keys, values);

    int i = -1;
    for (auto value : values) {
      i++;
      const uint256 hash(value);
      const string key = Strings::Format("%s%s", KVDB_PREFIX_BLOCK_OBJECT, hash.ToString().c_str());
      string blkvalue;
      kvdb.get(key, blkvalue);
      auto fb_block = flatbuffers::GetRoot<fbe::Block>(blkvalue.data());
      blkTs_.pushBlock(atoi(keys[i].substr(3)), fb_block->timestamp());
      LOG_DEBUG("height: %d, ts: %u", atoi(keys[i].substr(3)), fb_block->timestamp());
    }
    LOG_INFO("found max block timestamp: %lld", blkTs_.getMaxTimestamp());
  }

  currBlockHeight_ = log2BlockBeginHeight;
  currBlockHash_   = log2BlockBeginHash;
}

void Log2Producer::updateLog1FileStatus() {
  static int32_t log1FileIndex = -1;
  static int64_t currLog1FileOffset = -1;

  string sql;
  const string nowStr = date("%F %T");

  // index
  if (log1FileIndex != log1FileIndex_) {
    sql = Strings::Format("UPDATE `0_explorer_meta` SET `value`='%d',`updated_at`='%s' "
                          " WHERE `key`='log2producer.log1file.index'",
                          log1FileIndex_, nowStr.c_str());
    db_.updateOrThrowEx(sql);

    log1FileIndex = log1FileIndex_;
  }

  // offset
  if (currLog1FileOffset != currLog1FileOffset_) {
    sql = Strings::Format("UPDATE `0_explorer_meta` SET `value`='%lld',`updated_at`='%s' "
                          " WHERE `key`='log2producer.log1file.offset'",
                          currLog1FileOffset_, nowStr.c_str());
    db_.updateOrThrowEx(sql, 1);
    currLog1FileOffset = currLog1FileOffset_;
  }

}

void Log2Producer::syncLog1() {
  LogScope ls("Log2Producer::syncLog1()");

  // 初始化时，默认索引为零，偏移量为零
  log1FileIndex_  = 0;
  log1FileOffset_ = 0;

  // 从数据库读取最后记录的索引和偏移量
  string sql;
  MySQLResult res;
  char **row = nullptr;
  sql = "SELECT `key`,`value` FROM `0_explorer_meta` WHERE `key` IN ('log2producer.log1file.index', 'log2producer.log1file.offset') ";
  db_.query(sql, res);
  if (res.numRows() != 0) {
    assert(res.numRows() == 2);
    while ((row = res.nextRow()) != nullptr) {
      if (strcmp(row[0], "log2producer.log1file.index") == 0) {
        log1FileIndex_ = atoi(row[1]);
      }
      else if (strcmp(row[0], "log2producer.log1file.offset") == 0) {
        log1FileOffset_ = atoi64(row[1]);
      }
    }
    LOG_INFO("last log1 file, index: %d, offset: %lld", log1FileIndex_, log1FileOffset_);
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

void Log2Producer::tryReadLog1(vector<string> &lines, vector<int64_t> &offset) {
  const string currFile = Strings::Format("%s/files/%d.log",
                                          log1Dir_.c_str(), log1FileIndex_);
  const string nextFile = Strings::Format("%s/files/%d.log",
                                          log1Dir_.c_str(), log1FileIndex_ + 1);

  // 判断是否存在下一个文件，需要在读取当前文件之间判断，防止读取漏掉现有文件的最后内容
  const bool isNextExist = fs::exists(fs::path(nextFile));

  lines.clear();
  offset.clear();

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

    const int64_t currOffset = log1Ifstream.tellg();

    // offset肯定是同一个文件内的，不可能跨文件

    lines.push_back(line);
    offset.push_back(currOffset);
    log1FileOffset_ = currOffset;

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
  const string nowStr = date("%F %T");

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

  //
  // 检测 table.0_explorer_meta 是否存在记录：
  //     'log2producer.log1file.index', 'log2producer.log1file.offset'
  //
  {
    string sql = "SELECT `key` FROM `0_explorer_meta` WHERE `key` "
    "IN ('log2producer.log1file.index', 'log2producer.log1file.offset') ";
    MySQLResult res;
    db_.query(sql, res);
    if (res.numRows() > 0 && res.numRows() != 2) {
      THROW_EXCEPTION_DB("one of ('log2producer.log1file.index', 'log2producer.log1file.offset') is missing");
    }
    if (res.numRows() == 0) {
      sql = Strings::Format("INSERT INTO `0_explorer_meta` (`key`, `value`, `created_at`, `updated_at`)"
                            " VALUES ('log2producer.log1file.index',  '0', '%s', '%s'), "
                            "        ('log2producer.log1file.offset', '0', '%s', '%s') ",
                            nowStr.c_str(), nowStr.c_str(), nowStr.c_str(), nowStr.c_str());
      db_.updateOrThrowEx(sql, 2);
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

  inotify_.RemoveAll();
  changed_.notify_all();
}

void Log2Producer::handleTxAccept(Log1 &log1Item) {
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
  updateLog1FileStatus();
  db_.execute("COMMIT");
}

void Log2Producer::handleTxReject(Log1 &log1Item) {
  //
  // 删除的时候，我们先删除子交易，然后再删除自己，所以可能出现删除多笔交易的情况
  // bitcoind-v0.12.1 删除交易时，并不能严格按照依赖关系排序
  //
  string sql;
  const CTransaction &tx = log1Item.getTx();
  const uint256 hash = tx.GetHash();
  LOG_INFO("process tx(-): %s", hash.ToString().c_str());

  if (!memRepo_.isExist(hash)) {
    LOG_WARN("tx is not exist in mempool: %s", hash.ToString().c_str());
    return;
  }

  vector<uint256> removeHashes;
  memRepo_.removeTxAndChildTx(tx, removeHashes);

  // 批量删除
  vector<string> values;
  const string nowStr = date("%F %T");
  for (auto &it : removeHashes) {
    string item = Strings::Format("-1,%d,-1,-1,0,'%s','%s','%s'",
                                  LOG2TYPE_TX_REJECT,
                                  it.ToString().c_str(),
                                  nowStr.c_str(), nowStr.c_str());
    values.push_back(item);
  }

  // 插入本批次数据
  multiInsert(db_, "0_txlogs2", kTableTxlogs2Fields_, values);

  // 提交本批数据
  commitBatch(values.size());
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
  updateLog1FileStatus();
  db_.execute("COMMIT");
}

// 清理txlogs2的内存交易
void Log2Producer::clearMempoolTxs() {
  //
  // <del>由于 table.0_memrepo_txs 是有交易前后依赖顺序的，所以逆序读出，批量移除即可。
  // 即交易前后相关性借助 table.0_memrepo_txs 来完成。</del>
  //
  // 不可以依赖 table.0_memrepo_txs 的交易前后顺序，是可能有问题的. 应该遍历 memRepo_
  // 并逐个移除。
  //
  vector<string> values;
  const string nowStr = date("%F %T");

  // 遍历，移除所有内存交易（未确认）
  while (memRepo_.size() > 0) {
    const uint256 hash = memRepo_.removeTx();
    string item = Strings::Format("-1,%d,-1,-1,0,'%s','%s','%s'",
                                  LOG2TYPE_TX_REJECT,
                                  hash.ToString().c_str(),
                                  nowStr.c_str(), nowStr.c_str());
    values.push_back(item);
  }
  assert(memRepo_.size() == 0);

  // 插入本批次数据
  multiInsert(db_, "0_txlogs2", kTableTxlogs2Fields_, values);

  // 提交本批数据
  commitBatch(values.size());

  // 此时 table.0_memrepo_txs 应该为空的
  {
    MySQLResult res;
    string sql = "SELECT COUNT(*) FROM `0_memrepo_txs`";
    db_.query(sql, res);
    assert(res.numRows() == 1);
    char **row = nullptr;
    row = res.nextRow();
    if (atoi(row[0]) != 0) {
      THROW_EXCEPTION_DB("table.0_memrepo_txs SHOULD be empty");
    }
  }
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

  vector<string>  lines;
  vector<int64_t> offsets;

  while (running_) {
    lines.clear();
    offsets.clear();
    tryReadLog1(lines, offsets);

    if (!running_) { break; }

    if (lines.size() == 0) {
      UniqueLock ul(lock_);
      // 默认等待N毫秒，直至超时，中间有人触发，则立即continue读取记录
      changed_.wait_for(ul, chrono::milliseconds(10*1000));
      continue;
    }

    for (size_t i = 0; i < lines.size(); i++) {
      const string &line   = lines[i];
      currLog1FileOffset_ = offsets[i];

      Log1 log1Item;
      log1Item.parse(line);

      // Tx: accept
      if (log1Item.isAcceptTx()) {
        handleTxAccept(log1Item);
      }
      // Tx: remove, 并不一定是真正的reject，只是表示从 bitcoind mempool 移除交易
      else if (log1Item.isRemoveTx()) {
        handleTxReject(log1Item);
      }
      // Block
      else if (log1Item.isBlock()) {
        handleBlock(log1Item);
      }
      // Clear Mempool Txs
      else if (log1Item.isClearMemtxs()) {
        clearMempoolTxs();
      }
      else {
        THROW_EXCEPTION_DBEX("invalid log1 type, log line: %s", line.c_str());
      }
    } /* /for */

    // 通知
    doNotifyTParser();

  } /* /while */
}

