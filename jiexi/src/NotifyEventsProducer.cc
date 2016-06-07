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
#include "bitcoin/base58.h"
#include "bitcoin/util.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/file.h>

#include <boost/filesystem.hpp>

#include "rocksdb/cache.h"
#include "rocksdb/compaction_filter.h"
#include "rocksdb/db.h"
#include "rocksdb/options.h"
#include "rocksdb/slice.h"
#include "rocksdb/table.h"
#include "rocksdb/memtablerep.h"

#include "NotifyEventsProducer.h"

namespace fs = boost::filesystem;

//////////////////////////////  NotifyEventsProducer  //////////////////////////
NotifyEventsProducer::NotifyEventsProducer(): running_(false),
db_(Config::GConfig.get("mysql.uri")), currBlockHeight_(-1), currLog1FileOffset_(-1),
notifyEventsMaker_(nullptr)
{
  kTableNotifyLogsFields_ = "`batch_id`,`type`,`height`,`hash`,`created_at`";

  // watch file inotify
  log1Dir_ = Config::GConfig.get("log1.dir");
  notifyFileLog1Producer_ = log1Dir_ + "/NOTIFY_LOG1_TO_LOG2";
  watchNotifyThread_ = thread(&NotifyEventsProducer::threadWatchNotifyFile, this);
}

NotifyEventsProducer::~NotifyEventsProducer() {
  changed_.notify_all();
  if (watchNotifyThread_.joinable()) {
    watchNotifyThread_.join();
  }
  if (makeNotifyEventsThread_.joinable()) {
    makeNotifyEventsThread_.join();
  }

  if (notifyEventsMaker_) {
    delete notifyEventsMaker_;
    notifyEventsMaker_ = nullptr;
  }

  if (kvdb_ != nullptr) {
    delete kvdb_;
  }
}

void NotifyEventsProducer::openKVDB() {
  options_.compression = rocksdb::kSnappyCompression;
  options_.create_if_missing        = false;
  //  options_.disableDataSync          = true;   // disable syncing of data files

  //
  // open Rocks DB
  //
  // Optimize RocksDB. This is the easiest way to get RocksDB to perform well
  options_.IncreaseParallelism();
  options_.OptimizeLevelStyleCompaction();

  //
  // https://github.com/facebook/rocksdb/blob/master/tools/benchmark.sh
  //
  options_.target_file_size_base    = (size_t)128 * 1024 * 1024;
  options_.max_bytes_for_level_base = (size_t)1024 * 1024 * 1024;
  options_.num_levels = 6;

  options_.write_buffer_size = (size_t)128 * 1024 * 1024;
  options_.max_write_buffer_number  = 8;
  //  options_.disable_auto_compactions = true;
  //
  //  // params_bulkload
  //  options_.max_background_compactions = 16;
  //  options_.max_background_flushes = 7;
  //  options_.level0_file_num_compaction_trigger = (size_t)10 * 1024 * 1024;
  //  options_.level0_slowdown_writes_trigger     = (size_t)10 * 1024 * 1024;
  //  options_.level0_stop_writes_trigger         = (size_t)10 * 1024 * 1024;
  //
  //  // memtable_factory: vector
  //  options_.memtable_factory.reset(new rocksdb::VectorRepFactory);

  // initialize BlockBasedTableOptions
  auto cache1 = rocksdb::NewLRUCache(1 * 1024 * 1024 * 1024LL);
  rocksdb::BlockBasedTableOptions bbt_opts;
  bbt_opts.block_cache = cache1;
  bbt_opts.cache_index_and_filter_blocks = 0;
  bbt_opts.block_size = 32 * 1024;
  bbt_opts.format_version = 2;
  options_.table_factory.reset(rocksdb::NewBlockBasedTableFactory(bbt_opts));

  // open kvdb
  const string kvpath = Config::GConfig.get("rocksdb.txdb.path", ".");
  LOG_DEBUG("rocksdb path: %s", kvpath.c_str());
  rocksdb::Status s = rocksdb::DB::Open(options_, kvpath, &kvdb_);
  if (!s.ok()) {
    THROW_EXCEPTION_DBEX("open rocks db fail");
  }
}

void NotifyEventsProducer::loadMemrepoTxs() {
  //
  // 数据库中的交易不会有双花冲突，这里载入时即使不按照顺序也是不会有问题的
  //
  string sql = "SELECT `tx_hash` FROM `0_memrepo_txs` ORDER BY `position`";
  MySQLResult res;
  char **row = nullptr;
  db_.query(sql, res);
  if (res.numRows() == 0)  {
    return;
  }

  while ((row = res.nextRow()) != nullptr) {
    const uint256 hash(row[0]);
    CTransaction tx;
    getTxByHash(hash, tx);

    if (memRepo_.addTx(tx) == false) {
      THROW_EXCEPTION_DBEX("add tx to mem repo fail, tx: %s", hash.ToString().c_str());
    }
    LOG_DEBUG("load 0_memrepo_txs tx: %s", hash.ToString().c_str());
  }

  LOG_INFO("load 0_memrepo_txs txs count: %llu", memRepo_.size());
  memRepo_.ignoreUnsyncData();  // 忽略一下载入的数据，已经同步过了
}

void NotifyEventsProducer::initNotifyEvents() {
  LogScope ls("NotifyEventsProducer::initNotifyEvents()");
  string sql;
  MySQLResult res;
  char **row;

  //
  // 清理临时的记录（未完整状态的）
  //
  {
    sql = "DELETE FROM `0_notify_logs` WHERE `batch_id` = -1";
    db_.update(sql);
  }

  // 获取最后状态信息
  {
    string sql = "SELECT `value` FROM `0_notify_meta` WHERE `key`='notifyevents.status'";
    db_.query(sql, res);
    assert(res.numRows() == 1);
    row = res.nextRow();
    const vector<string> arr = split(row[0], '|');

    // log1FileIndex | log1FileOffset | blockHeight | blockHash
    log1FileIndex_   = atoi(arr[0].c_str());
    log1FileOffset_  = atoi64(arr[1].c_str());
    currBlockHeight_ = atoi(arr[2].c_str());
    currBlockHash_   = arr.size() >= 4 ? uint256(arr[3]) : uint256();
  }

  if (currBlockHeight_ < 0) {
    // 没有记录，则以配置文件的块信息作为起始块
    currBlockHeight_ = (int32_t)Config::GConfig.getInt("notifyevents.begin.block.height");
    currBlockHash_   = uint256(Config::GConfig.get("notifyevents.begin.block.hash"));
  }

  if (currBlockHash_ == uint256() || currBlockHeight_ < 0) {
    THROW_EXCEPTION_DBEX("invalid latest block: %d, %s",
                         currBlockHeight_, currBlockHash_.ToString().c_str());
  }
  LOG_INFO("latest status, log1FileIndex: %d, log1FileOffset: %lld, height: %d, hash: %s",
           log1FileIndex_, log1FileOffset_,
           currBlockHeight_, currBlockHash_.ToString().c_str());

  // 载入内存库中(未确认)交易
  loadMemrepoTxs();
}

void NotifyEventsProducer::updateCosumeStatus() {
  static string _lastStatus;

  string sql;
  const string nowStr = date("%F %T");

  // status
  const string currStatus = Strings::Format("%d|%lld|%d|%s",
                                        log1FileIndex_, currLog1FileOffset_,
                                        currBlockHeight_, currBlockHash_.ToString().c_str());
  if (_lastStatus != currStatus) {
    sql = Strings::Format("UPDATE `0_notify_meta` SET `value`='%s',`updated_at`='%s' "
                          " WHERE `key`='notifyevents.status'",
                          currStatus.c_str(), nowStr.c_str());
    db_.updateOrThrowEx(sql, 1);

    _lastStatus = currStatus;
  }
}

void NotifyEventsProducer::tryRemoveOldLog1() {
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

void NotifyEventsProducer::tryReadLog1(vector<string> &lines, vector<int64_t> &offset) {
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

void NotifyEventsProducer::checkEnvironment() {
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
  // 检测 table.0_notify_meta 是否存在记录：'notifyevents.status'
  // log1FileIndex | log1FileOffset | blockHeight | blockHash
  //
  {
    string sql = "SELECT `key` FROM `0_notify_meta` WHERE `key`='notifyevents.status'";
    MySQLResult res;
    db_.query(sql, res);
    if (res.numRows() == 0) {
      sql = Strings::Format("INSERT INTO `0_notify_meta` (`key`, `value`, `created_at`, `updated_at`)"
                            " VALUES ('notifyevents.status',  '0|0|-1|', '%s', '%s')",
                            nowStr.c_str(), nowStr.c_str());
      db_.updateOrThrowEx(sql, 1);
    }
  }
}

void NotifyEventsProducer::init() {
  running_ = true;

  checkEnvironment();
  openKVDB();
  initNotifyEvents();

  // 启动生成events线程
  notifyEventsMaker_ = new NotifyEventsMaker(this);
  makeNotifyEventsThread_ = thread(&NotifyEventsProducer::threadMakeNotifyEvents, this);
}

void NotifyEventsProducer::stop() {
  LOG_INFO("stop NotifyEventsProducer...");
  running_ = false;

  inotify_.RemoveAll();
  changed_.notify_all();

  notifyEventsMaker_->stop();
}

void NotifyEventsProducer::setRawBlock(const CBlock &blk) {
  ScopeLock sl(kvLock_);

  // kv key is block hash
  const string key = blk.GetHash().ToString();
  string value;
//  LOG_DEBUG("setRawBlock: %s", key.c_str());

  // check if exist
  rocksdb::Status s = kvdb_->Get(rocksdb::ReadOptions(), key, &value);
  if (!s.IsNotFound()) {
    return;
  }

  // put raw hex
  const string hex = EncodeHexBlock(blk);
  const rocksdb::Slice skey(key.data(), key.size());
  const rocksdb::Slice svalue((const char *)hex.data(), hex.size());
  kvdb_->Put(rocksdb::WriteOptions(), skey, svalue);
}

void NotifyEventsProducer::setRawTx(const CTransaction &tx) {
  ScopeLock sl(kvLock_);

  // kv key is txhash
  const string key = tx.GetHash().ToString();
  string value;
//  LOG_DEBUG("setRawTx: %s", key.c_str());

  // check if exist
  rocksdb::Status s = kvdb_->Get(rocksdb::ReadOptions(), key, &value);
  if (!s.IsNotFound()) {
    return;
  }

  // put raw hex tx
  const string txhex = EncodeHexTx(tx);
  const rocksdb::Slice skey(key.data(), key.size());
  const rocksdb::Slice svalue((const char *)txhex.data(), txhex.size());
  kvdb_->Put(rocksdb::WriteOptions(), skey, svalue);
}

void NotifyEventsProducer::getBlockByHash(const uint256 &hash, CBlock &blk) {
  ScopeLock sl(kvLock_);

  const string key = hash.ToString();
  string value;
//  LOG_DEBUG("getBlockByHash: %s", key.c_str());

  rocksdb::Status s = kvdb_->Get(rocksdb::ReadOptions(), key, &value);
  if (s.IsNotFound()) {
    THROW_EXCEPTION_DBEX("can't find block, hash: %s", key.c_str());
  }

  blk.SetNull();
  if (!DecodeHexBlk(blk, value)) {
    THROW_EXCEPTION_DBEX("decode block fail, hash: %s, hex: %s", key.c_str(), value.c_str());
  }
}

void NotifyEventsProducer::getTxByHash(const uint256 &txHash, CTransaction &tx) {
  ScopeLock sl(kvLock_);

  const string key = txHash.ToString();
  string value;
//  LOG_DEBUG("getTxByHash: %s", key.c_str());

  rocksdb::Status s = kvdb_->Get(rocksdb::ReadOptions(), key, &value);
  if (s.IsNotFound()) {
    THROW_EXCEPTION_DBEX("can't find tx, hash: %s", key.c_str());
  }

  tx.SetNull();
  if (!DecodeHexTx(tx, value)) {
    THROW_EXCEPTION_DBEX("decode tx fail, hash: %s, hex: %s", key.c_str(), value.c_str());
  }
}

void NotifyEventsProducer::handleTxAccept(Log1 &log1Item) {
  //
  // 接收新的交易
  //
  string sql;
  const CTransaction &tx = log1Item.getTx();
  const uint256 hash = tx.GetHash();
  LOG_INFO("process tx(+): %s", hash.ToString().c_str());

  if (memRepo_.isExist(hash)) {
    LOG_WARN("tx is already exist in mempool: %s", hash.ToString().c_str());
    return;
  }

  vector<uint256> conflictTxs;
  const string nowStr = date("%F %T");
  const bool res = memRepo_.addTx(tx);

  // 冲突的交易
  if (res == false) {
    LOG_WARN("reject tx: %s", hash.ToString().c_str());
    return;
  }

  // 插入row txs，保证只要处理过的 txhash 都可以查询到 raw hex. 查前向 tx inputs需要用到
  setRawTx(tx);

  // 无冲突，插入DB
  sql = Strings::Format("INSERT INTO `0_notify_logs` (`batch_id`, `type`, "
                        " `height`, `hash`,`created_at`) "
                        " VALUES ("
                        " (SELECT IFNULL(MAX(`batch_id`), 0) + 1 FROM `0_notify_logs` as t1), "
                        " %d, -1, '%s', '%s');",
                        NOTIFYLOG_TYPE_TX_ACCEPT, hash.ToString().c_str(), nowStr.c_str());

  db_.execute("START TRANSACTION");
  db_.updateOrThrowEx(sql, 1);
  memRepo_.syncToDB(db_);
  updateCosumeStatus();
  db_.execute("COMMIT");
}

void NotifyEventsProducer::handleTxReject(Log1 &log1Item) {
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
    string item = Strings::Format("-1,%d,-1,'%s','%s'",
                                  NOTIFYLOG_TYPE_TX_REJECT,
                                  it.ToString().c_str(), nowStr.c_str());
    values.push_back(item);
  }

  // 插入本批次数据
  multiInsert(db_, "0_notify_logs", kTableNotifyLogsFields_, values);

  // 提交本批数据
  commitBatch(values.size());
}

void NotifyEventsProducer::handleBlockAccept(Log1 &log1Item) {
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
  setRawBlock(blk);

  // 2.1 插入 raw_txs
  // 这里保证了，能够查询到 raw_block 时，则其中的所有 raw_tx 都能查询到
  for (const auto &tx : blk.vtx) {
    setRawTx(tx);
  }

  // 3.0 批量插入数据
  vector<string> values;
  const string nowStr = date("%F %T");

  // 冲突的交易，需要做拒绝处理，注意顺序
  // conflictTxs 中其实是逆序，因为前面是先剔除冲突交易树的叶子节点
  for (auto &it : conflictTxs) {
    string item = Strings::Format("-1,%d,-1,'%s','%s'",
                                  NOTIFYLOG_TYPE_TX_REJECT,
                                  it.ToString().c_str(), nowStr.c_str());
    values.push_back(item);
  }

  // 块事件：块接收
  values.push_back(Strings::Format("-1,%d,%d,'%s','%s'",
                                   NOTIFYLOG_TYPE_BLOCK_ACCEPT, log1Item.blockHeight_,
                                   hash.ToString().c_str(), nowStr.c_str()));

  // 新块的交易，做确认操作
  for (auto &tx : blk.vtx) {
    string item;
    const uint256 txhash = tx.GetHash();

    // 首次处理的，需要补 accept 操作
    if (alreadyInMemTxHashs.find(txhash) == alreadyInMemTxHashs.end()) {
      item = Strings::Format("-1,%d,-1,'%s','%s'",
                             NOTIFYLOG_TYPE_TX_ACCEPT,
                             txhash.ToString().c_str(), nowStr.c_str());
      values.push_back(item);
    }

    // confirm
    item = Strings::Format("-1,%d,%d,'%s','%s'",
                           NOTIFYLOG_TYPE_TX_CONFIRM, log1Item.blockHeight_,
                           txhash.ToString().c_str(), nowStr.c_str());
    values.push_back(item);
  }

  // 插入本批次数据
  multiInsert(db_, "0_notify_logs", kTableNotifyLogsFields_, values);

  // 提交本批数据
  commitBatch(values.size());

  LOG_INFO("block txs: %llu, conflict txs: %llu", blk.vtx.size(), conflictTxs.size());
}

void NotifyEventsProducer::commitBatch(const size_t expectAffectedRows) {
  string sql;
  MySQLResult res;
  char **row;

  // fetch next batch_id
  sql = "SELECT IFNULL(MAX(`batch_id`), 0) + 1 FROM `0_notify_logs`";
  db_.query(sql, res);
  row = res.nextRow();
  const int64_t nextBatchID = atoi64(row[0]);

  // update batch_id
  sql = Strings::Format("UPDATE `0_notify_logs` SET `batch_id`=%lld WHERE `batch_id`=-1",
                        nextBatchID);
  //
  // 使用事务提交，保证更新成功的数据就是既定的数量。有差错则异常，DB事务无法提交。
  //
  db_.execute("START TRANSACTION");
  db_.updateOrThrowEx(sql, (int32_t)expectAffectedRows);
  memRepo_.syncToDB(db_);
  updateCosumeStatus();
  db_.execute("COMMIT");
}

// 清理txlogs2的内存交易
void NotifyEventsProducer::clearMempoolTxs() {
  //
  // 不可以依赖 table.0_memrepo_txs 的交易前后顺序，是可能有问题的. 应该遍历 memRepo_
  // 并逐个移除。
  //
  vector<string> values;
  const string nowStr = date("%F %T");

  // 遍历，移除所有内存交易（未确认）
  while (memRepo_.size() > 0) {
    const uint256 hash = memRepo_.removeTx();
    string item = Strings::Format("-1,%d,-1,'%s','%s'",
                                  NOTIFYLOG_TYPE_TX_REJECT,
                                  hash.ToString().c_str(), nowStr.c_str());
    values.push_back(item);
  }
  assert(memRepo_.size() == 0);

  // 插入本批次数据
  multiInsert(db_, "0_notify_logs", kTableNotifyLogsFields_, values);

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

void NotifyEventsProducer::handleBlockRollback(const int32_t height, const CBlock &blk) {
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

  // 块事件：块拒绝
  values.push_back(Strings::Format("-1,%d,%d,'%s','%s'",
                                   NOTIFYLOG_TYPE_BLOCK_REJECT, height,
                                   hash.ToString().c_str(), nowStr.c_str()));

  // 新块的交易，做反确认操作，反序遍历
  for (auto tx = blk.vtx.rbegin(); tx != blk.vtx.rend(); ++tx) {
    string item = Strings::Format("-1,%d,%d,'%s','%s'",
                                  NOTIFYLOG_TYPE_TX_UNCONFIRM, height,
                                  tx->GetHash().ToString().c_str(),
                                  nowStr.c_str());
    values.push_back(item);
  }

  // coinbase tx 需要 reject
  {
    string item = Strings::Format("-1,%d,%d,'%s','%s'",
                                  NOTIFYLOG_TYPE_TX_REJECT, height,
                                  blk.vtx[0].GetHash().ToString().c_str(),
                                  nowStr.c_str());
    values.push_back(item);
  }

  // 插入本批次数据
  multiInsert(db_, "0_notify_logs", kTableNotifyLogsFields_, values);

  // 提交本批数据
  commitBatch(values.size());

  LOG_INFO("block txs: %llu", blk.vtx.size());
}

void NotifyEventsProducer::handleBlock(Log1 &log1Item) {
  //
  // 块高度前进
  //
  if (log1Item.blockHeight_ > currBlockHeight_) {
    assert(log1Item.blockHeight_ == currBlockHeight_ + 1);

    // 设置当前高度和哈希
    currBlockHeight_ = log1Item.blockHeight_;
    currBlockHash_   = log1Item.getBlock().GetHash();
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
    getBlockByHash(currBlockHash_, currBlock);
    assert(currBlock.hashPrevBlock == log1Item.getBlock().GetHash());

    currBlockHeight_ = log1Item.blockHeight_;
    currBlockHash_   = log1Item.getBlock().GetHash();
    handleBlockRollback(currBlockHeight_, currBlock);
  }
  else {
    THROW_EXCEPTION_DBEX("block are same height, log1: %s",
                         log1Item.toString().c_str());
  }
}

void NotifyEventsProducer::threadWatchNotifyFile() {
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

void NotifyEventsProducer::threadMakeNotifyEvents() {
  notifyEventsMaker_->init();
  notifyEventsMaker_->run();
}

void NotifyEventsProducer::run() {
  LogScope ls("NotifyEventsProducer::run()");

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
      if (!running_) { break; }

      const string &line  = lines[i];
      currLog1FileOffset_ = offsets[i];

      Log1 log1Item;
      log1Item.parse(line);

      // Tx: accept
      if (log1Item.isAcceptTx()) {
        handleTxAccept(log1Item);
      }
      // Tx: remove
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
  } /* /while */
}



///////////////////////////////  NotifyEventsMaker  ////////////////////////////
NotifyEventsMaker::NotifyEventsMaker(NotifyEventsProducer *notifyEventsProducer):
running_(false),
db_(Config::GConfig.get("mysql.uri")), notifyEventsProducer_(notifyEventsProducer),
lastNotifyLogId_(-1), lastEventsTableIdx_(-1)
{
}

NotifyEventsMaker::~NotifyEventsMaker() {

}

void NotifyEventsMaker::init() {
  db_.open();

  if (!getLastStatus()) {
    const string nowStr = date("%F %T");
    string sql = Strings::Format("INSERT INTO `0_notify_meta` (`key`, `value`, `created_at`, `updated_at`)"
                                 " VALUES ('notifyevents.maker.lastlogid',  '0', '%s', '%s')",
                                 nowStr.c_str(), nowStr.c_str());
    db_.updateOrThrowEx(sql, 1);
    getLastStatus();
  }
  assert(lastNotifyLogId_ != -1);

  running_ = true;
}

void NotifyEventsMaker::stop() {
  if (running_) {
    running_ = false;
    LOG_INFO("NotifyEventsMaker::stop()...");
  }
}

bool NotifyEventsMaker::getLastStatus() {
  string sql;
  MySQLResult res;
  char **row = nullptr;

  sql = "SELECT `value` FROM `0_notify_meta` WHERE `key`='notifyevents.maker.lastlogid'";
  db_.query(sql, res);
  if (res.numRows() == 0) {
    return false;
  }

  row = res.nextRow();
  lastNotifyLogId_ = atoi64(row[0]);
  return true;
}

void NotifyEventsMaker::updateStatus() {
  string sql = Strings::Format("UPDATE `0_notify_meta` SET `value`='%lld',`updated_at`='%s' "
                               " WHERE `key`='notifyevents.maker.lastlogid'",
                               lastNotifyLogId_, date("%F %T").c_str());
  db_.updateOrThrowEx(sql, 1);
}

void NotifyEventsMaker::tryGetNotifyLog(vector<NotifyLog> &logs) {
  logs.clear();

  MySQLResult res;
  char **row = nullptr;
  string sql = Strings::Format("SELECT `id`,`type`,`height`,`hash` FROM `0_notify_logs` "
                               " WHERE `id`> %lld ORDER BY `id` ASC LIMIT 50000",
                               lastNotifyLogId_);
  db_.query(sql, res);
  if (res.numRows() == 0) {
    return;
  }

  logs.resize(res.numRows());
  size_t i = 0;
  while ((row = res.nextRow()) != nullptr) {
    assert(row[3] != nullptr);  // hash 总是存在的：块、交易
    logs[i].id_     = atoi64(row[0]);
    logs[i].type_   = atoi  (row[1]);
    logs[i].height_ = atoi  (row[2]);
    logs[i].hash_   = uint256(row[3]);
    i++;
  }
}

void _getOutputBalanceDiff(const CTxOut &ptxout,
                           map<string, int64_t> &balanceDiff,
                           bool isInputs) {
  txnouttype type;
  vector<CTxDestination> addresses;
  int nRequired;
  if (!ExtractDestinations(ptxout.scriptPubKey, type, addresses, nRequired)) {
    return;
  }

  set<CTxDestination> addresses_set(addresses.begin(), addresses.end());
  for (const CTxDestination& addr : addresses_set) {
    int64_t value = isInputs ? -1*ptxout.nValue : ptxout.nValue;
    balanceDiff[CBitcoinAddress(addr).ToString()] += value;
  }
}

void NotifyEventsMaker::getAddressBalanceDiff(const NotifyLog &notifyLog,
                                              map<string, int64_t> &balanceDiff) {
  CTransaction tx;
  notifyEventsProducer_->getTxByHash(notifyLog.hash_, tx);

  // inputs
  if (!tx.IsCoinBase()) {
    for (const CTxIn &txin : tx.vin) {
      CTransaction prev_tx;
      notifyEventsProducer_->getTxByHash(txin.prevout.hash, prev_tx);
      const CTxOut &ptxout = prev_tx.vout[txin.prevout.n];
      _getOutputBalanceDiff(ptxout, balanceDiff, true/* inputs */);
    }
  }

  // outputs
  for (const CTxOut &txout : tx.vout) {
    _getOutputBalanceDiff(txout, balanceDiff, false/* outputs */);
  }
}

bool NotifyEventsMaker::isEventsTableExist(const int32_t tableIdx) {
  static set<int32_t> _tableIdxCache;

  if (_tableIdxCache.count(tableIdx)) {
    return true;
  }

  // 判断表是否存在
  MySQLResult res;
  string sql = Strings::Format("SHOW TABLES LIKE '%s'", tableIdx2Name(tableIdx).c_str());
  db_.query(sql, res);

  if (res.numRows() == 1) {
    _tableIdxCache.insert(tableIdx);
    return true;
  }

  return false;
}

void NotifyEventsMaker::checkEventsTable(const int32_t tableIdx) {
  // 首次设置table index
  if (lastEventsTableIdx_ == -1) {
    lastEventsTableIdx_ = tableIdx;
  }

  if (isEventsTableExist(tableIdx)) {
    return;
  }
  assert(tableIdx == lastEventsTableIdx_ + 1);

  // 提交当前的未提交数据，可能存在未提交数据。这里会提交EOF记录，必须保证EOF之后不会再有记录
  commitToDB();

  string sql;
  // 创建新表
  sql = Strings::Format("CREATE TABLE `%s` LIKE `0_tpl_events`",
                        tableIdx2Name(tableIdx).c_str());
  db_.updateOrThrowEx(sql);

  // 移除旧表
  tryToRemoveOldTable(tableIdx);

  // 上一个表中插入 EOF 记录
  sql = Strings::Format("INSERT INTO `%s` (`type`, `height`, `address`, "
                        " `balance_diff`, `hash`, `created_at`) "
                        " VALUES ('__EOF__', '0', '', '0', '', '%s');",
                        tableIdx2Name(lastEventsTableIdx_).c_str(),
                        date("%F %T").c_str());
  db_.update(sql);

  // 切换表索引
  lastEventsTableIdx_ = tableIdx;
}

void NotifyEventsMaker::tryToRemoveOldTable(const int32_t tableIdx) {
  const int32_t maxTableNum = (int32_t)Config::GConfig.getInt("notifyevents.table.max.num", 30);
  if (tableIdx - maxTableNum < 0) { return; }

  string sql = Strings::Format("DROP TABLE IF EXISTS `%s`",
                               tableIdx2Name(tableIdx - maxTableNum).c_str());
  db_.update(sql);
}

const char *NotifyEventsMaker::getTypeStr(const int32_t type) {
  switch (type) {
    case NOTIFYLOG_TYPE_TX_ACCEPT:
      return "TX_ACCEPT";
    case NOTIFYLOG_TYPE_TX_CONFIRM:
      return "TX_CONFIRM";
    case NOTIFYLOG_TYPE_TX_UNCONFIRM:
      return "TX_UNCONFIRM";
    case NOTIFYLOG_TYPE_TX_REJECT:
      return "TX_REJECT";
    case NOTIFYLOG_TYPE_BLOCK_ACCEPT:
      return "BLOCK_ACCEPT";
    case NOTIFYLOG_TYPE_BLOCK_REJECT:
      return "BLOCK_REJECT";
    default:
      break;
  }
  return "UNKNOWN";
}

void NotifyEventsMaker::tryToRemoveNotifyLogs(const int64_t notifyLogId) {
  // 最多保留 300万 条记录
  const int64_t kMaxkeepItems = 3000000;

  // 每 20万 触发一次
  if (notifyLogId % 200000 != 0 || notifyLogId < kMaxkeepItems) {
    return;
  }

  LOG_INFO("delete 0_notify_logs where id < %lld ", notifyLogId - kMaxkeepItems);
  string sql = Strings::Format("DELETE FROM `0_notify_logs` WHERE `id` < %lld",
                               notifyLogId - kMaxkeepItems);
  db_.update(sql);
}

string NotifyEventsMaker::tableIdx2Name(const int32_t tableIdx) {
  return Strings::Format("events_%08d", tableIdx);
}

int32_t NotifyEventsMaker::tableIndex(const int64_t notifyLogId) {
  //
  // 每条交易对应的地址总体看来是一个几乎恒定的系数，所以我们每消费N条就分一个表.
  // 用notifyLog.id除一下即可得到表序号
  //
  return (int32_t)(notifyLogId / 500000);
}

void NotifyEventsMaker::writeNotifyEvents(const NotifyLog &notifyLog,
                                          const map<string, int64_t> &balanceDiff) {
  const int32_t tableIdx = tableIndex(notifyLog.id_);
  const string tableName = tableIdx2Name(tableIdx);
  checkEventsTable(tableIdx);

  const string nowStr = date("%F %T");
  if (notifyLog.type_ == NOTIFYLOG_TYPE_BLOCK_ACCEPT ||
      notifyLog.type_ == NOTIFYLOG_TYPE_BLOCK_REJECT)
  {
    // 块类型消息
    string item = Strings::Format("'%s',%d,'',0,'%s','%s'",
                                  getTypeStr(notifyLog.type_),
                                  notifyLog.height_,
                                  notifyLog.hash_.ToString().c_str(),
                                  nowStr.c_str());
    eventsValues_.push_back(item);
  }
  else
  {
    // 交易类型消息
    for (const auto &it : balanceDiff) {
      string item = Strings::Format("'%s',%d,'%s',%lld,'%s','%s'",
                                    getTypeStr(notifyLog.type_),
                                    notifyLog.height_, it.first.c_str(),
                                    it.second, notifyLog.hash_.ToString().c_str(),
                                    nowStr.c_str());
      eventsValues_.push_back(item);
    }
  }

  lastNotifyLogId_ = notifyLog.id_;  // updateStatus() will read it

  tryToRemoveNotifyLogs(notifyLog.id_);
}

void NotifyEventsMaker::commitToDB() {
  const string kTableFields = "`type`,`height`,`address`,`balance_diff`,`hash`,`created_at`";
  const string tableName = tableIdx2Name(lastEventsTableIdx_);

  if (eventsValues_.empty()) {
    return;
  }

  db_.execute("START TRANSACTION");
  multiInsert(db_, tableName, kTableFields, eventsValues_);
  updateStatus();
  db_.execute("COMMIT");

  eventsValues_.clear();
}

void NotifyEventsMaker::run() {
  LogScope ls("NotifyEventsMaker::run()");
  vector<NotifyLog> notifyLogs;

  while (running_) {
    // try to get notify logs
    tryGetNotifyLog(notifyLogs);
    if (notifyLogs.size() == 0) {
      sleepMs(250);
      continue;
    }

    LOG_INFO("load notify logs: %llu", notifyLogs.size());
    for (const auto &notifyLog : notifyLogs) {
      if (!running_) { break; }

      map<string, int64_t> balanceDiff;
      if (!(notifyLog.type_ == NOTIFYLOG_TYPE_BLOCK_ACCEPT ||
            notifyLog.type_ == NOTIFYLOG_TYPE_BLOCK_REJECT)) {
        // 时间类型为交易，则获取其地址金额变更记录
        getAddressBalanceDiff(notifyLog, balanceDiff);
      }
      writeNotifyEvents(notifyLog, balanceDiff);

      // 每条记录不到 200 字节，每2万提交一次
      if (eventsValues_.size() > 20000) {
        commitToDB();
      }
    } /* /for */
    commitToDB();

    notifyLogs.clear();
  } /* /while */
}




