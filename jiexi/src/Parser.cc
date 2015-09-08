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

#include <string>
#include <iostream>
#include <fstream>

#include <boost/thread.hpp>
#include <boost/filesystem.hpp>

#include "Parser.h"
#include "Common.h"
#include "Util.h"
#include "Log2Producer.h"

#include "bitcoin/base58.h"
#include "bitcoin/util.h"

std::unordered_map<int64_t/*address ID*/, LastestAddressInfo *> gAddrTxCache;

/////////////////////////////////  RawBlock  ///////////////////////////////////

RawBlock::RawBlock(const int64_t blockId, const int32_t height, const int32_t chainId,
                   const uint256 hash, const string &hex) {
  blockId_ = blockId;
  height_  = height;
  chainId_ = chainId;
  hash_    = hash;
  hex_     = hex;
}

////////////////////////////  LastestAddressInfo  //////////////////////////////

LastestAddressInfo::LastestAddressInfo(int64_t addrId,
                                       int32_t beginTxYmd, int32_t endTxYmd,
                                       int64_t beginTxId, int64_t endTxId,
                                       int64_t unconfirmedReceived, int64_t unconfirmedSent,
                                       int32_t lastConfirmedTxYmd, int64_t lastConfirmedTxId,
                                       int64_t totalReceived, int64_t totalSent,
                                       int64_t txCount) {
  addrId_  = addrId;

  beginTxYmd_    = beginTxYmd;
  endTxYmd_      = endTxYmd;
  beginTxId_     = beginTxId;
  endTxId_       = endTxId;

  unconfirmedReceived_ = unconfirmedReceived;
  unconfirmedSent_     = unconfirmedSent;

  lastConfirmedTxYmd_ = lastConfirmedTxYmd;
  lastConfirmedTxId_  = lastConfirmedTxId;

  totalReceived_ = totalReceived;
  totalSent_     = totalSent;

  txCount_       = txCount;

  lastUseTime_ = time(nullptr);
}

LastestAddressInfo::LastestAddressInfo(const LastestAddressInfo &a) {
  addrId_  = a.addrId_;

  beginTxYmd_    = a.beginTxYmd_;
  endTxYmd_      = a.endTxYmd_;
  beginTxId_     = a.beginTxId_;
  endTxId_       = a.endTxId_;

  unconfirmedReceived_ = a.unconfirmedReceived_;
  unconfirmedSent_     = a.unconfirmedSent_;

  lastConfirmedTxYmd_ = a.lastConfirmedTxYmd_;
  lastConfirmedTxId_  = a.lastConfirmedTxId_;

  totalReceived_ = a.totalReceived_;
  totalSent_     = a.totalSent_;

  txCount_       = a.txCount_;

  lastUseTime_ = a.lastUseTime_;
}

// 获取block raw hex/id/chainId...
static
void _getBlockRawHexByBlockId(const int64_t blockId,
                              MySQLConnection &db, string *rawHex) {
  string sql;

  // 从DB获取raw block
  MySQLResult res;
  sql = Strings::Format("SELECT `hex` FROM `0_raw_blocks` WHERE `id` = %lld",
                        blockId);

  char **row = nullptr;
  db.query(sql, res);
  if (res.numRows() == 0) {
    THROW_EXCEPTION_DBEX("can't find block by blkID from DB: %lld", blockId);
  }

  row = res.nextRow();
  if (rawHex != nullptr) {
    rawHex->assign(row[0]);
    assert(rawHex->length() % 2 == 0);
  }
}


//////////////////////////////////  TxLog2  ////////////////////////////////////
//
// blkHeight:
//   -2   无效的块高度
//   -1   临时块（未确认的交易组成的虚拟块）
//   >= 0 有效的块高度
//
TxLog2::TxLog2(): id_(0), type_(0), blkHeight_(-2), blkId_(0),
maxBlkTimestamp_(0), ymd_(0), txId_(0)
{
}

TxLog2::TxLog2(const TxLog2 &t) {
  id_           = t.id_;
  type_         = t.type_;
  blkHeight_    = t.blkHeight_;
  blkId_        = t.blkId_;
  maxBlkTimestamp_ = t.maxBlkTimestamp_;
  ymd_          = t.ymd_;
  createdAt_    = t.createdAt_;

  txHex_  = t.txHex_;
  txHash_ = t.txHash_;
  tx_     = t.tx_;
  txId_   = t.txId_;
}

TxLog2::~TxLog2() {}

string TxLog2::toString() const {
  string s = Strings::Format("TxLogs2: (id: %lld, type: %d, height: %d, block_id: %lld, "
                             "max block timestamp: %u, ymd: %d, created_at: %s)",
                             id_, type_, blkHeight_, blkId_, maxBlkTimestamp_,
                             ymd_, createdAt_.c_str());
  return s;
}




//////////////////////////////  CacheManager  //////////////////////////////////

CacheManager::CacheManager(const string  SSDBHost,
                           const int32_t SSDBPort, const string dirURL):
  ssdb_(nullptr), SSDBHost_(SSDBHost), SSDBPort_(SSDBPort)
{
  // create connection
  ssdb_ = ssdb::Client::connect(SSDBHost_, SSDBPort_);
  if (ssdb_ == nullptr) {
    THROW_EXCEPTION_DBEX("fail to connect to ssdb server. host: %s, port: %d",
                         SSDBHost_.c_str(), SSDBPort_);
  }
  running_   = true;
  ssdbAlive_ = true;

  // remove last '/'
  dirURL_ = dirURL;
  if (dirURL_.length() > 1 && dirURL_[dirURL_.length() - 1] == '/') {
    dirURL_.resize(dirURL_.length() - 1);
  }
  // create file if not exist
  if (dirURL_.size() > 0 &&
      !boost::filesystem::exists(dirURL_) &&
      !boost::filesystem::create_directories(dirURL_)) {
    THROW_EXCEPTION_DBEX("create folder failed: %s", dirURL_.c_str());
  }

  // setup consumer thread
  boost::thread t(boost::bind(&CacheManager::threadConsumer, this));
  runningThreadConsumer_ = true;
}

CacheManager::~CacheManager() {
  // 等待数据消费完，最大等待N秒
  for (int i = 59; i >= 0; i--) {
    lock_.lock();
    if (qkv_.size() == 0 && qhashset_.size() == 0) {
      lock_.unlock();
      break;
    }
    lock_.unlock();

    sleep(2);
    LOG_WARN("waiting for CacheManager to finish(%d),"
             " qkv_.size(): %lld, qhashset_.size(): %lld",
             i, qkv_.size(), qhashset_.size());
    if (i == 0) {
      LOG_WARN("reach max try times, focus to stop");
    }
  }

  running_ = false;

  while (runningThreadConsumer_) {  // 等待消费线程退出
    sleepMs(50);
  }

  if (ssdb_ != nullptr) {
    delete ssdb_;
    ssdb_ = nullptr;
  }
}

void CacheManager::insertKV(const string &key) {
  if (qkvTemp_.find(key) != qkvTemp_.end()) {
    return;
  }

  string url;
  if (strncmp(key.c_str(), "tx_", 3) == 0) {
    url = Strings::Format("/rawtx/%s", key.substr(3).c_str());
  }
  else if (strncmp(key.c_str(), "addr_", 5) == 0) {
    url = Strings::Format("/address/%s", key.substr(5).c_str());
  }
  else if (strncmp(key.c_str(), "blkh_", 5) == 0) {
    url = Strings::Format("/block-height/%s", key.substr(5).c_str());
  }
  else if (strncmp(key.c_str(), "blk_", 4) == 0) {
    url = Strings::Format("/rawblock/%s", key.substr(4).c_str());
  }
  if (url.length() > 0) {
    qUrlTemp_.insert(url);
    LOG_DEBUG("[CacheManager::insertKV] url: %s", url.c_str());
  }

  qkvTemp_.insert(key);
  LOG_DEBUG("[CacheManager::insertKV] key: %s", key.c_str());
}

void CacheManager::insertHashSet(const string &address,
                                 const string &tableName) {
  const string s = address + "." + tableName;
  if (qhashsetTempSet_.find(s) != qhashsetTempSet_.end()) {
    return;  // already exist, ignore
  }

  qhashsetTempSet_.insert(s);
  qhashsetTemp_.push_back(std::make_pair(address, tableName));
  LOG_DEBUG("[CacheManager::insertHashSet] address:%s, tableName: %s",
            address.c_str(), tableName.c_str());
}

void CacheManager::commit() {
  ScopeLock sl(lock_);

  // KV
  // 超过500万，丢弃数据，防止撑爆内存
  if (qkv_.size() > 500 * 10000) {
    vector<string>().swap(qkv_);
  }
  for (auto &it : qkvTemp_) {
    qkv_.push_back(it);
  }
  qkvTemp_.clear();

  // Hash SET
  // 超过500万，丢弃数据，防止撑爆内存
  if (qhashset_.size() > 500 * 10000) {
    vector<std::pair<string, string> >().swap(qhashset_);
  }
  for (auto &it : qhashsetTemp_) {
    qhashset_.push_back(std::make_pair(it.first, it.second));
  }
  qhashsetTemp_.clear();
  qhashsetTempSet_.clear();

  // URL
  // 超过500万，丢弃数据，防止撑爆内存
  if (qUrl_.size() > 500 * 10000) {
    vector<string>().swap(qUrl_);
  }
  for (auto &it : qUrlTemp_) {
    qUrl_.push_back(it);
  }
  qUrlTemp_.clear();
}

void CacheManager::flushURL(vector<string> &buf) {
  {
    ScopeLock sl(lock_);
    buf.insert(buf.end(), qUrl_.begin(), qUrl_.end());
    qUrl_.clear();
  }
  if (buf.size() == 0) {
    return;
  }

  const string name = Strings::Format("%s/%s",
                                      dirURL_.length() ? dirURL_.c_str() : ".",
                                      date("%Y%m%d.%H%M%S").c_str());
  const string nameTmp = name + ".tmp";

  // write to disk
  std::ofstream file(nameTmp, std::ios_base::app | std::ios_base::out);
  if (!file.is_open()) {
    LOG_ERROR("open file fail: %s", name.c_str());
    return;
  }
  for (auto &line : buf) {
    LOG_DEBUG("line: %s", line.c_str());
    file << line << "?skipcache=1" << std::endl;
  }
  file.close();

  LOG_DEBUG("[CacheManager::flushURL] write %lld to file: %s", buf.size(), name.c_str());

  // rename
  boost::filesystem::rename(nameTmp, name);
  buf.clear();
}

//
// SSDB数据结构文档：http://twiki.bitmain.com/bin/view/Main/SSDB-Cache
//
void CacheManager::threadConsumer() {
  vector<string> buf, bufUrl;
  vector<std::pair<string, string> > bufHashSet;
  buf.reserve(1024);
  bufUrl.reserve(1024);
  bufHashSet.reserve(1024);
  ssdb::Status s;
  int64_t successCount, itemCount;
  time_t lastFlushTime = time(nullptr) - 10;

  while (running_) {
    assert(ssdbAlive_ == true);
    assert(ssdb_ != nullptr);
    itemCount = successCount = 0;

    // Key - Value
    {
      ScopeLock sl(lock_);
      buf.insert(buf.end(), qkv_.begin(), qkv_.end());
      qkv_.clear();
    }
    if (buf.size()) {
      itemCount += buf.size();
      s = ssdb_->multi_del(buf);
      LOG_DEBUG("ssdb KV del, count: %lld", buf.size());
      if (s.error()) {
        LOG_ERROR("ssdb KV del fail");
      } else {
        successCount += buf.size();
      }
    }

    // HashSet: 目前HashSet类型就一个: addr_table_{addr_id}
    {
      ScopeLock sl(lock_);
      bufHashSet.insert(bufHashSet.end(), qhashset_.begin(), qhashset_.end());
      qhashset_.clear();
    }
    if (bufHashSet.size()) {
      itemCount += bufHashSet.size();
      LOG_DEBUG("ssdb HashSet del, count: %lld", bufHashSet.size());
      for (auto &it : bufHashSet) {
        const string name = "addr_table_" + it.first;
        s = ssdb_->hdel(name, it.second);
        if (s.error()) {
          LOG_ERROR("ssdb HashSet del fail: %s, %s",
                    name.c_str(), it.second.c_str());
        } else {
          successCount++;
        }
      }
    }

    // flush URL
    if (lastFlushTime + 60 < time(nullptr)) {
      lastFlushTime = time(nullptr);
      flushURL(bufUrl);
    }

    if (itemCount != successCount) {
      LOG_ERROR("ssdb some items failure, total: %lld, success: %lld",
                itemCount, successCount);
    }

    if (itemCount == 0) {
      sleepMs(250);  // 暂无数据
    } else if (itemCount == successCount) {
      // 全部执行成功，清理数据
      buf.clear();
      bufHashSet.clear();
    }

    // 成功数量为零，说明SSDB server端出故障了，尝试重新连接
    if (itemCount > 0 && successCount == 0) {
      delete ssdb_;
      ssdb_      = nullptr;
      ssdbAlive_ = false;

      while (running_) {
        LOG_INFO("we lost ssdb server, try reconnect...");
        sleep(1);

        ssdb_ = ssdb::Client::connect(SSDBHost_, SSDBPort_);
        if (ssdb_ != nullptr) {
          ssdbAlive_ = true;
          LOG_INFO("reconnect ssdb success");
          break;
        }
        LOG_ERROR("reconnect to ssdb server fail. host: %s, port: %d",
                  SSDBHost_.c_str(), SSDBPort_);
      }
    }
  } /* /while */

  runningThreadConsumer_ = false;
}


/////////////////////////////////  AddressTxNode  ///////////////////////////////////
AddressTxNode::AddressTxNode() {
  memset(&ymd_, 0, sizeof(AddressTxNode));
}

AddressTxNode::AddressTxNode(const AddressTxNode &node) {
  memcpy(&ymd_, &node.ymd_, sizeof(AddressTxNode));
}

void AddressTxNode::reset() {
  memset(&ymd_, 0, sizeof(AddressTxNode));
}


/////////////////////////////////  Parser  ///////////////////////////////////
Parser::Parser():dbExplorer_(Config::GConfig.get("db.explorer.uri")),
running_(true), isReverse_(false), reverseEndTxlog2ID_(0), cache_(nullptr),
unconfirmedTxsSize_(0), unconfirmedTxsCount_(0)
{
  // setup cache manager: SSDB
  cacheEnable_ = Config::GConfig.getBool("ssdb.enable", false);
  if (cacheEnable_) {
    cache_ = new CacheManager(Config::GConfig.get("ssdb.host"),
                              (int32_t)Config::GConfig.getInt("ssdb.port"),
                              Config::GConfig.get("url.file.dir", ""));
  }
}

Parser::~Parser() {
  stop();

  if (cache_ != nullptr) {
    delete cache_;
    cache_ = nullptr;
  }
}

void Parser::setReverseMode(const int64_t endTxlogID) {
  isReverse_  = true;
  reverseEndTxlog2ID_ = endTxlogID;
}

void Parser::stop() {
  if (running_) {
    running_ = false;
    LOG_INFO("stop tparser");
  }
}

bool Parser::init() {
  string sql;
  MySQLResult res;
  char **row = nullptr;

  if (!dbExplorer_.ping()) {
    LOG_FATAL("connect to explorer DB failure");
    return false;
  }

  // 检测DB参数： max_allowed_packet
  const int32_t maxAllowed = atoi(dbExplorer_.getVariable("max_allowed_packet").c_str());
  const int32_t kMinAllowed = 64*1024*1024;
  if (maxAllowed < kMinAllowed) {
    LOG_FATAL("mysql.db.max_allowed_packet(%d) is too small, should >= %d",
              maxAllowed, kMinAllowed);
    return false;
  }

  //
  // jiexi.unconfirmed_txs.count & jiexi.unconfirmed_txs.size
  //
  {
    sql = Strings::Format("DELETE FROM `0_explorer_meta` WHERE `key` IN "
                          " ('jiexi.unconfirmed_txs.count', 'jiexi.unconfirmed_txs.size')");
    dbExplorer_.updateOrThrowEx(sql);

    sql = Strings::Format("SELECT COUNT(*), IFNULL(SUM(`size`), 0) FROM `0_unconfirmed_txs`");
    dbExplorer_.query(sql, res);
    row = res.nextRow();
    unconfirmedTxsCount_ = atoi(row[0]);
    unconfirmedTxsSize_  = atoi64(row[0]);

    sql = Strings::Format("INSERT INTO `0_explorer_meta` (`key`, `value`, `created_at`, `updated_at`)"
                          " VALUES ('jiexi.unconfirmed_txs.count',  %d, NOW(), NOW()), "
                          "        ('jiexi.unconfirmed_txs.size', %lld, NOW(), NOW()) ",
                          unconfirmedTxsCount_, unconfirmedTxsSize_);
    dbExplorer_.updateOrThrowEx(sql, 2);
  }

  return true;
}


void Parser::run() {
  TxLog2 txLog2, lastTxLog2;
  string blockHash;  // 目前仅用在URL回调上
  int64_t lastTxLog2Id = 0;

  //
  // 启动时：如果是反向消费，则需要将 lastTxLog2Id 向后加一。即使 lastTxLog2Id 不连续也OK。
  //
  if (isReverse_) {
    lastTxLog2Id = getLastTxLog2Id();
    updateLastTxlog2Id(lastTxLog2Id + 1);
    LOG_INFO("[reverse start] set lastTxLog2Id from %lld to %lld",
             lastTxLog2Id, lastTxLog2Id + 1);
  }

  while (running_) {
    lastTxLog2Id = getLastTxLog2Id();

    if (tryFetchTxLog2(&txLog2, lastTxLog2Id) == false) {
      sleepMs(100);
      continue;
    }

    // 反向消费，达到停止txlog ID
    if (txLog2.id_ < reverseEndTxlog2ID_) {
      LOG_WARN("current txlogs2 ID (%lld) is less than stop ID (%lld), stopping...",
               txLog2.id_, reverseEndTxlog2ID_);
      stop();
      break;
    }

    // 检测 address_txs_<YYYYMM> 是否存在
    checkTableAddressTxs(txLog2.maxBlkTimestamp_);

    //
    // DB START TRANSACTION
    //
    if (!dbExplorer_.execute("START TRANSACTION")) {
      goto error;
    }

    if (txLog2.type_ == LOG2TYPE_TX_ACCEPT) {
      if (txLog2.tx_.IsCoinBase()) {
        acceptBlock(&txLog2, blockHash);
      }
      acceptTx(&txLog2);
    }
    else if (txLog2.type_ == LOG2TYPE_TX_CONFIRM) {
      confirmTx(&txLog2);
    }
    else if (txLog2.type_ == LOG2TYPE_TX_UNCONFIRM) {
      unconfirmTx(&txLog2);
    }
    else if (txLog2.type_ == LOG2TYPE_TX_REJECT) {
      if (txLog2.tx_.IsCoinBase()) {
        rejectBlock(&txLog2);
      }
      rejectTx(&txLog2);
    }
    else {
      LOG_FATAL("invalid txlog2 type: %d", txLog2.type_);
    }

    // 设置为当前的ID，该ID不一定连续，不可以 lastID++
    updateLastTxlog2Id(txLog2.id_);

    //
    // DB COMMIT
    //
    if (!dbExplorer_.execute("COMMIT")) {
      goto error;
    }
    lastTxLog2 = txLog2;

    writeLastProcessTxlogTime();

    if (cache_ != nullptr) {
      cache_->commit();
    }

    if (blockHash.length()) {  // 目前仅用在URL回调上，仅使用一次
      callBlockRelayParseUrl(blockHash);
      blockHash.clear();
    }

  } /* /while */

  //
  // 终止时：如果是反向消费，则需要将txlog offset向前减一。即使txlogID不连续也OK。
  //
  if (isReverse_) {
    lastTxLog2Id = getLastTxLog2Id();
    updateLastTxlog2Id(lastTxLog2Id - 1);
    LOG_INFO("[reverse end] set lastTxLog2Id from %lld to %lld",
             lastTxLog2Id, lastTxLog2Id - 1);
  }

  return;

error:
  dbExplorer_.execute("ROLLBACK");
  return;
}


void Parser::writeLastProcessTxlogTime() {
  // 写最后消费txlog的时间，30秒之内仅写一次
  static time_t lastTime = 0;

  time_t now = time(nullptr);
  if (now > lastTime + 30) {
    lastTime = now;
    writeTime2File("lastProcessTxLogTime.txt", now);
  }
}

// 检测表是否存在，`create table` 这样的语句会导致DB事务隐形提交，必须摘离出事务之外
void Parser::checkTableAddressTxs(const uint32_t timestamp) {
  if (timestamp == 0) { return; }

  static std::set<int32_t> addressTxs;
  MySQLResult res;
  string sql;

  const int32_t ymd = atoi(date("%Y%m%d", timestamp).c_str());
  if (addressTxs.find(ymd) != addressTxs.end()) {
    return;
  }

  // show table like to check if exist
  const string tName = Strings::Format("address_txs_%d", tableIdx_AddrTxs(ymd));
  sql = Strings::Format("SHOW TABLES LIKE '%s'", tName.c_str());
  dbExplorer_.query(sql, res);
  if (res.numRows() > 0) {
    addressTxs.insert(ymd);
    return;
  }

  // create if not exist
  sql = Strings::Format("CREATE TABLE `%s` LIKE `0_tpl_address_txs`", tName.c_str());
  dbExplorer_.updateOrThrowEx(sql);
  addressTxs.insert(ymd);
}

// 根据Hash，查询ID
bool Parser::txsHash2ids(const std::set<uint256> &hashVec,
                         std::map<uint256, int64_t> &hash2id) {
  MySQLResult res;
  string sql;
  char **row;

  // tx <-> raw_tx， hash/ID均一一对应，表数量一致
  for (auto &hash : hashVec) {
    if (hash2id.find(hash) != hash2id.end()) {
      continue;
    }
    const string hashStr = hash.ToString();
    sql = Strings::Format("SELECT `id` FROM `raw_txs_%04d` WHERE `tx_hash`='%s'",
                          HexToDecLast2Bytes(hashStr) % 64, hashStr.c_str());
    dbExplorer_.query(sql, res);
    if (res.numRows() != 1) {
      LOG_FATAL("can't find tx's ID, hash: %s", hashStr.c_str());
      return false;
    }
    row = res.nextRow();
    hash2id.insert(std::make_pair(hash, atoi64(row[0])));
  }

  assert(hash2id.size() >= hashVec.size());
  return true;
}

void Parser::updateLastTxlog2Id(const int64_t newId) {
  string sql = Strings::Format("UPDATE `0_explorer_meta` SET `value` = '%lld',`updated_at`='%s' "
                               " WHERE `key`='jiexi.last_txlog2_offset'",
                               newId, date("%F %T").c_str());
  dbExplorer_.updateOrThrowEx(sql, 1);
}

void _insertBlock(MySQLConnection &db, const CBlock &blk,
                  const int64_t blockId, const int32_t height,
                  const int32_t blockBytes) {
  CBlockHeader header = blk.GetBlockHeader();  // alias
  string prevBlockHash = header.hashPrevBlock.ToString();
  MySQLResult res;
  char **row = nullptr;
  string sql;

  // 查询前向块信息，前向块信息必然存在(创始块没有前向块)
  int64_t prevBlockId = 0;
  if (height > 0) {
    sql = Strings::Format("SELECT `block_id` FROM `0_blocks` WHERE `hash` = '%s'",
                          prevBlockHash.c_str());
    db.query(sql, res);
    if (res.numRows() != 1) {
      THROW_EXCEPTION_DBEX("prev block not exist in DB, hash: ", prevBlockHash.c_str());
    }
    row = res.nextRow();
    prevBlockId = atoi64(row[0]);
    assert(prevBlockId > 0);
  }

  //
  // 将当前高度的块的chainID增一，若存在分叉的话. UNIQUE	(height, chain_id)
  // 当前即将插入的块的chainID必然为0
  // 0_raw_blocks.chain_id 与 0_blocks.chain_id 同一个块的 chain_id 不一定一致
  //
  sql = Strings::Format("SELECT `block_id` FROM `0_blocks` "
                        " WHERE `height` = %lld ORDER BY `chain_id` DESC ",
                        height);
  db.query(sql, res);
  while ((row = res.nextRow()) != nullptr) {
    const string sql2 = Strings::Format("UPDATE `0_blocks` SET `chain_id`=`chain_id`+1 "
                                        " WHERE `block_id` = %lld ", atoi64(row[0]));
    db.updateOrThrowEx(sql2, 1);
  }

  // 构造插入SQL
  sql = Strings::Format("SELECT `block_id` FROM `0_blocks` WHERE `block_id`=%lld",
                        blockId);
  db.query(sql, res);
  if (res.numRows() == 0) {  // 不存在则插入，由于有rollback等行为，块由可能已经存在了
    uint64_t difficulty      = 0;
    double difficulty_double = 0.0;
    BitsToDifficulty(header.nBits, difficulty);
    BitsToDifficulty(header.nBits, difficulty_double);

    const int64_t rewardBlock = GetBlockValue(height, 0);
    const int64_t rewardFees  = blk.vtx[0].GetValueOut() - rewardBlock;
    assert(rewardFees >= 0);
    string sql1 = Strings::Format("INSERT INTO `0_blocks` (`block_id`, `height`, `hash`,"
                                  " `version`, `mrkl_root`, `timestamp`, `bits`, `nonce`,"
                                  " `prev_block_id`, `prev_block_hash`, `next_block_id`, "
                                  " `next_block_hash`, `chain_id`, `size`,"
                                  " `difficulty`, `difficulty_double`, "
                                  " `tx_count`, `reward_block`, `reward_fees`, "
                                  " `created_at`) VALUES ("
                                  // 1. `block_id`, `height`, `hash`, `version`, `mrkl_root`, `timestamp`
                                  " %lld, %d, '%s', %d, '%s', %u, "
                                  // 2. `bits`, `nonce`, `prev_block_id`, `prev_block_hash`,
                                  " %u, %u, %lld, '%s', "
                                  // 3. `next_block_id`, `next_block_hash`, `chain_id`, `size`,
                                  " 0, '', %d, %d, "
                                  // 4. `difficulty`, `difficulty_double`, `tx_count`,
                                  " %llu, %f, %d, "
                                  // 5. `reward_block`, `reward_fees`, `created_at`
                                  " %lld, %lld, '%s');",
                                  // 1.
                                  blockId, height,
                                  header.GetHash().ToString().c_str(),
                                  header.nVersion,
                                  header.hashMerkleRoot.ToString().c_str(),
                                  (uint32_t)header.nTime,
                                  // 2.
                                  header.nBits, header.nNonce, prevBlockId, prevBlockHash.c_str(),
                                  // 3.
                                  0/* chainId */, blockBytes,
                                  // 4.
                                  difficulty, difficulty_double, blk.vtx.size(),
                                  // 5.
                                  rewardBlock, rewardFees, date("%F %T").c_str());
    db.updateOrThrowEx(sql1, 1);
  }

  // 更新前向块信息, 高度一以后的块需要，创始块不需要
  if (height > 0) {
    string sql2 = Strings::Format("UPDATE `0_blocks` SET `next_block_id`=%lld, `next_block_hash`='%s'"
                                  " WHERE `hash` = '%s' ",
                                  blockId, header.GetHash().ToString().c_str(),
                                  prevBlockHash.c_str());
    db.updateOrThrowEx(sql2, 1);
  }
}

// 已经存在则会忽略
void _insertBlockTxs(MySQLConnection &db, const CBlock &blk, const int64_t blockId,
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

  // 仅允许两种情况，要么记录全部都存在，要么记录都不存在
  if (existMaxPosition != -1) {
    LOG_WARN("block_txs already exist");
    // 数量不符合，异常
    if (existMaxPosition != (int32_t)blk.vtx.size() - 1) {
      THROW_EXCEPTION_DBEX("exist txs is not equal block.vtx, now position is %d, should be %d",
                           existMaxPosition, (int32_t)blk.vtx.size() - 1);
    }
    return;  // 记录全部都存在
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
  if (!multiInsert(db, tableName, fields, values)) {
    THROW_EXCEPTION_DBEX("multi insert failure, table: '%s'", tableName.c_str());
  }
}

// 接收一个新块
void Parser::acceptBlock(TxLog2 *txLog2, string &blockHash) {
  // 获取块Raw Hex
  string blkRawHex;
  _getBlockRawHexByBlockId(txLog2->blkId_, dbExplorer_, &blkRawHex);

  // 解码Raw Hex
  CBlock blk;
  if (!DecodeHexBlk(blk, blkRawHex)) {
    THROW_EXCEPTION_DBEX("decode block hex failure, hex: %s", blkRawHex.c_str());
  }

  blockHash = blk.GetHash().ToString();

  // 拿到 tx_hash -> tx_id 的对应关系
  // 要求"导入"模块(log2producer)：存入所有的区块交易(table.raw_txs_xxxx)
  std::set<uint256> hashVec;
  std::map<uint256, int64_t> hash2id;
  for (auto & it : blk.vtx) {
    hashVec.insert(it.GetHash());
  }
  txsHash2ids(hashVec, hash2id);

  // 插入数据至 table.0_blocks
  _insertBlock(dbExplorer_, blk, txLog2->blkId_,
               txLog2->blkHeight_, (int32_t)blkRawHex.length()/2);

  // 插入数据至 table.block_txs_xxxx
  _insertBlockTxs(dbExplorer_, blk, txLog2->blkId_, hash2id);
}


void _removeUnspentOutputs(MySQLConnection &db,
                           const int64_t txId, const int32_t position,
                           map<int64_t, int64_t> &addressBalance) {
  MySQLResult res;
  string sql;
  string tableName;
  char **row;
  int n;

  // 获取相关output信息
  tableName = Strings::Format("tx_outputs_%04d", txId % 100);
  sql = Strings::Format("SELECT `address_ids`,`value` FROM `%s` WHERE `tx_id`=%lld AND `position`=%d",
                        tableName.c_str(), txId, position);
  db.query(sql, res);
  row = res.nextRow();
  assert(res.numRows() == 1);

  // 获取地址
  const string s = string(row[0]);
  const int64_t value = atoi64(row[1]);
  vector<string> addressIdsStrVec = split(s, '|');
  n = -1;
  for (auto &addrIdStr : addressIdsStrVec) {
    n++;
    const int64_t addrId = atoi64(addrIdStr.c_str());

    // 减扣该地址额度
    addressBalance[addrId] += -1 * value;

    // 将 address_unspent_outputs_xxxx 相关记录删除
    tableName = Strings::Format("address_unspent_outputs_%04d", addrId % 10);
    sql = Strings::Format("DELETE FROM `%s` WHERE `address_id`=%lld AND `tx_id`=%lld "
                          " AND `position`=%d AND `position2`=%d ",
                          tableName.c_str(), addrId, txId, position, n);
    db.updateOrThrowEx(sql, 1);
  }
}


DBTxOutput getTxOutput(MySQLConnection &db, const int64_t txId, const int32_t position) {
  MySQLResult res;
  char **row;
  string sql;
  DBTxOutput o;

  sql = Strings::Format("SELECT `value`,`spent_tx_id`,`spent_position`,`address`,`address_ids` "
                        " FROM `tx_outputs_%04d` WHERE `tx_id`=%lld AND `position`=%d ",
                        txId % 100, txId, position);
  db.query(sql, res);
  if (res.numRows() != 1) {
    THROW_EXCEPTION_DBEX("can't find tx_outputs, txId: %lld, position: %d",
                         txId, position);
  }
  row = res.nextRow();
  
  o.txId     = txId;
  o.position = position;
  o.value    = atoi64(row[0]);
  o.spentTxId     = atoi64(row[1]);
  o.spentPosition = atoi(row[2]);
  o.address    = string(row[3]);
  o.addressIds = string(row[4]);

  return o;
}

static
void _accpetTx_insertTxInputs(MySQLConnection &db, CacheManager *cache,
                              TxLog2 *txLog2, map<int64_t, int64_t> &addressBalance,
                              int64_t &valueIn) {
  int n;
  const string tableName = Strings::Format("tx_inputs_%04d", txLog2->txId_ % 100);
  const string now = date("%F %T");
  // table.tx_inputs_xxxx
  const string fields = "`tx_id`, `position`, `input_script_asm`, `input_script_hex`,"
  " `sequence`, `prev_tx_id`, `prev_position`, `prev_value`,"
  " `prev_address`, `prev_address_ids`, `created_at`";
  vector<string> values;
  string sql;

  n = -1;
  for (auto &in : txLog2->tx_.vin) {
    n++;
    uint256 prevHash;
    int64_t prevTxId;
    int32_t prevPos;

    if (txLog2->tx_.IsCoinBase()) {
      prevHash = 0;
      prevTxId = 0;
      prevPos  = -1;

      // 插入当前交易的inputs, coinbase tx的 scriptSig 不做decode，可能含有非法字符
      // 通常无法解析成功
      // coinbase无法担心其长度，bitcoind对coinbase tx的coinbase字段长度做了限制
      values.push_back(Strings::Format("%lld,%d,'','%s',%u,"
                                       "0,-1,0,'','','%s'",
                                       txLog2->txId_, n,
                                       HexStr(in.scriptSig.begin(), in.scriptSig.end()).c_str(),
                                       in.nSequence, now.c_str()));
    } else
    {
      prevHash = in.prevout.hash;
      prevTxId = txHash2Id(db, prevHash);
      prevPos  = (int32_t)in.prevout.n;

      // 将前向交易标记为已花费
      sql = Strings::Format("UPDATE `tx_outputs_%04d` SET "
                            " `spent_tx_id`=%lld, `spent_position`=%d"
                            " WHERE `tx_id`=%lld AND `position`=%d "
                            " AND `spent_tx_id`=0 AND `spent_position`=-1 ",
                            prevTxId % 100, txLog2->txId_, n,
                            prevTxId, prevPos);
      db.updateOrThrowEx(sql, 1);

      // 将 address_unspent_outputs_xxxx 相关记录删除
      _removeUnspentOutputs(db, prevTxId, prevPos, addressBalance);

      // 插入当前交易的inputs
      DBTxOutput dbTxOutput = getTxOutput(db, prevTxId, prevPos);
      if (dbTxOutput.txId == 0) {
        THROW_EXCEPTION_DBEX("can't find tx output, txId: %lld, hash: %s, position: %d",
                             prevTxId, prevHash.ToString().c_str(), prevPos);
      }
      values.push_back(Strings::Format("%lld,%d,'%s','%s',%u,%lld,%d,"
                                       "%lld,'%s','%s','%s'",
                                       txLog2->txId_, n,
                                       in.scriptSig.ToString().c_str(),
                                       HexStr(in.scriptSig.begin(), in.scriptSig.end()).c_str(),
                                       in.nSequence, prevTxId, prevPos,
                                       dbTxOutput.value,
                                       dbTxOutput.address.c_str(),
                                       dbTxOutput.addressIds.c_str(),
                                       now.c_str()));
      valueIn += dbTxOutput.value;

      if (cache != nullptr) {
        // http://twiki.bitmain.com/bin/view/Main/SSDB-Cache
        // 遍历inputs所关联tx，全部删除： tx_{hash}
        cache->insertKV(Strings::Format("tx_%s", prevHash.ToString().c_str()));

        // 清理所有输入地址
        if (dbTxOutput.address.length()) {
          auto addrStrVec = split(dbTxOutput.address, '|');
          for (auto &singleAddrStr : addrStrVec) {
            cache->insertKV(Strings::Format("addr_%s", singleAddrStr.c_str()));
            // 清理 address_txs_xxxx 的缓存计数器
            cache->insertHashSet(singleAddrStr, Strings::Format("address_txs_%d",
                                                                tableIdx_AddrTxs(txLog2->ymd_)));
          }
        }
      }
    }
  } /* /for */

  // 执行插入 inputs
  if (!multiInsert(db, tableName, fields, values)) {
    THROW_EXCEPTION_DBEX("insert inputs fail, txId: %lld, hash: %s",
                         txLog2->txId_, txLog2->tx_.GetHash().ToString().c_str());
  }
}


static
void _accpetTx_insertTxOutputs(MySQLConnection &db, CacheManager *cache,
                               TxLog2 *txLog2, map<int64_t, int64_t> &addressBalance) {
  int n;
  const string now = date("%F %T");
  set<string> allAddresss;

  // 提取涉及到的所有地址
  for (auto &out : txLog2->tx_.vout) {
    txnouttype type;
    vector<CTxDestination> addresses;
    int nRequired;
    if (!ExtractDestinations(out.scriptPubKey, type, addresses, nRequired)) {
      LOG_WARN("extract destinations failure, txId: %lld, hash: %s",
               txLog2->txId_, txLog2->tx_.GetHash().ToString().c_str());
      continue;
    }
    for (auto &addr : addresses) {  // multiSig 可能由多个输出地址
      const string addrStr = CBitcoinAddress(addr).ToString();
      allAddresss.insert(CBitcoinAddress(addr).ToString());
    }
  }
  // 拿到所有地址的id
  map<string, int64_t> addrMap;
  GetAddressIds(db, allAddresss, addrMap);

  if (cache != nullptr) {
    for (auto &singleAddrStr : allAddresss) {
      cache->insertKV(Strings::Format("addr_%s", singleAddrStr.c_str()));
      cache->insertHashSet(singleAddrStr, Strings::Format("address_txs_%d",
                                                          tableIdx_AddrTxs(txLog2->ymd_)));
    }
  }

  // 处理输出
  // (`address_id`, `tx_id`, `position`, `position2`, `block_height`, `value`, `created_at`)
  n = -1;
  vector<string> itemValues;
  for (auto &out : txLog2->tx_.vout) {
    n++;
    string addressStr;
    string addressIdsStr;
    txnouttype type;
    vector<CTxDestination> addresses;
    int nRequired;
    if (!ExtractDestinations(out.scriptPubKey, type, addresses, nRequired)) {
      LOG_WARN("extract destinations failure, txId: %lld, hash: %s, position: %d",
               txLog2->txId_, txLog2->tx_.GetHash().ToString().c_str(), n);
    }

    // multiSig 可能由多个输出地址: https://en.bitcoin.it/wiki/BIP_0011
    int i = -1;
    for (auto &addr : addresses) {
      i++;
      const string addrStr = CBitcoinAddress(addr).ToString();
      const int64_t addrId = addrMap[addrStr];
      addressStr    += addrStr + ",";
      addressIdsStr += Strings::Format("%lld", addrId) + ",";

      // 增加每个地址的余额
      addressBalance[addrId] += out.nValue;

      // 每一个输出地址均生成一条 address_unspent_outputs 记录
      string sql = Strings::Format("INSERT INTO `address_unspent_outputs_%04d`"
                                   " (`address_id`, `tx_id`, `position`, `position2`, `block_height`, `value`, `created_at`)"
                                   " VALUES (%lld, %lld, %d, %d, %d, %lld, '%s') ",
                                   addrId % 10, addrId, txLog2->txId_, n, i, txLog2->blkHeight_,
                                   out.nValue, now.c_str());
      db.updateOrThrowEx(sql, 1);
    }

    // 去掉拼接的最后一个逗号
    if (addressStr.length())
      addressStr.resize(addressStr.length() - 1);
    if (addressIdsStr.length())
      addressIdsStr.resize(addressIdsStr.length() - 1);

    // tx_outputs
    // Parser::init(): 当前MySQL max_allowed_packet 为 64MB
    string outputScriptAsm = out.scriptPubKey.ToString();
    const string outputScriptHex = HexStr(out.scriptPubKey.begin(), out.scriptPubKey.end());
    if (outputScriptAsm.length() > 16*1024*1024 ||
        (outputScriptAsm.length() > 1*1024*1024 &&
         outputScriptAsm.length() > outputScriptHex.length() * 4)) {
      outputScriptAsm = "";
    }
    // output Hex奇葩的交易：
    // http://tbtc.blockr.io/tx/info/c333a53f0174166236e341af9cad795d21578fb87ad7a1b6d2cf8aa9c722083c
    itemValues.push_back(Strings::Format("%lld,%d,'%s','%s',"
                                         "%lld,'%s','%s','%s',"
                                         "0,-1,'%s','%s'",
                                         // `tx_id`,`position`,`address`,`address_ids`
                                         txLog2->txId_, n, addressStr.c_str(), addressIdsStr.c_str(),
                                         // `value`,`output_script_asm`,`output_script_hex`,`output_script_type`
                                         out.nValue,
                                         outputScriptAsm.c_str(),
                                         outputScriptHex.length() < 32*1024*1024 ? outputScriptHex.c_str() : "",
                                         GetTxnOutputType(type) ? GetTxnOutputType(type) : "",
                                         // `spent_tx_id`,`spent_position`,`created_at`,`updated_at`
                                         now.c_str(), now.c_str()));
  }

  // table.tx_outputs_xxxx
  const string tableNameTxOutputs = Strings::Format("tx_outputs_%04d", txLog2->txId_ % 100);
  const string fieldsTxOutputs = "`tx_id`,`position`,`address`,`address_ids`,`value`,"
  "`output_script_asm`,`output_script_hex`,`output_script_type`,"
  "`spent_tx_id`,`spent_position`,`created_at`,`updated_at`";
  // multi insert outputs
  if (!multiInsert(db, tableNameTxOutputs, fieldsTxOutputs, itemValues)) {
    THROW_EXCEPTION_DBEX("insert outputs fail, txId: %lld, hash: %s",
                         txLog2->txId_, txLog2->tx_.GetHash().ToString().c_str());
  }
}

// 获取地址信息
static LastestAddressInfo *_getAddressInfo(MySQLConnection &db, const int64_t addrID) {
  MySQLResult res;
  char **row;
  string sql;
  const time_t now = time(nullptr);

  const string addrTableName = Strings::Format("addresses_%04d", tableIdx_Addr(addrID));
  std::unordered_map<int64_t, LastestAddressInfo *>::iterator it;

  it = gAddrTxCache.find(addrID);
  if (it == gAddrTxCache.end()) {
    sql = Strings::Format("SELECT `begin_tx_ymd`,`begin_tx_id`,`end_tx_ymd`,`end_tx_id`,`total_received`,"
                          " `total_sent`,`tx_count`, `unconfirmed_received`, `unconfirmed_sent`, "
                          " `last_confirmed_tx_id`, `last_confirmed_tx_ymd` "
                          " FROM `%s` WHERE `id`=%lld ",
                          addrTableName.c_str(), addrID);
    db.query(sql, res);
    if (res.numRows() != 1) {
      THROW_EXCEPTION_DBEX("can't find address record, addrId: %lld", addrID);
    }
    row = res.nextRow();
    auto ptr = new LastestAddressInfo(addrID,
                                      atoi(row[0]),     // beginTxYmd
                                      atoi(row[2]),     // endTxYmd
                                      atoi64(row[1]),   // beginTxId
                                      atoi64(row[3]),   // endTxId
                                      atoi64(row[7]),   // unconfirmedReceived
                                      atoi64(row[8]),   // unconfirmedSent
                                      atoi(row[10]),    // lastConfirmedTxYmd
                                      atoi64(row[9]),   // lastConfirmedTxId
                                      atoi64(row[4]),   // totalReceived
                                      atoi64(row[5]),   // totalSent
                                      atoi64(row[6]));  // totalSent

    gAddrTxCache[addrID] = ptr;
    it = gAddrTxCache.find(addrID);

    // clear, 数量限制，防止长时间运行后占用过多内存
    const int32_t kExpiredDays = 2;
    if (gAddrTxCache.size() > 500 * 10000) {
      for (auto it2 = gAddrTxCache.begin(); it2 != gAddrTxCache.end(); ) {
        if (it2->second->lastUseTime_ + 86400 * kExpiredDays > now) {
          it2++;
          continue;
        }
        // 删除超出超过 kExpiredDays 天的记录
        delete it2->second;
        gAddrTxCache.erase(it2++);
      }
    }
  }

  assert(it != gAddrTxCache.end());
  it->second->lastUseTime_ = now;  // update last use time
  return it->second;
}

// 变更地址&地址对应交易记录
static
void _accpetTx_insertAddressTxs(MySQLConnection &db, class TxLog2 *txLog2,
                                const map<int64_t, int64_t> &addressBalance) {
  string sql;

  for (auto &it : addressBalance) {
    const int64_t addrID      = it.first;
    const int64_t balanceDiff = it.second;
    const string addrTableName = Strings::Format("addresses_%04d", tableIdx_Addr(addrID));

    // 获取地址信息
    LastestAddressInfo *addr = _getAddressInfo(db, addrID);

    //
    // 处理前向记录
    // accept: 加入交易链尾，不涉及调整交易链
    //
    if (addr->endTxYmd_ > 0 && addr->endTxId_ > 0) {
      // 更新前向记录
      sql = Strings::Format("UPDATE `address_txs_%d` SET `next_ymd`=%d, `next_tx_id`=%lld "
                            " WHERE `address_id`=%lld AND `tx_id`=%lld ",
                            tableIdx_AddrTxs(addr->endTxYmd_), txLog2->ymd_,
                            txLog2->txId_, addrID, addr->endTxId_);
      db.updateOrThrowEx(sql, 1);

      if (addr->endTxYmd_ > txLog2->ymd_) {
        THROW_EXCEPTION_DBEX("addr's(%lld) endTxYmd(%d) is over than txLog2->ymd_(%d), %s",
                             addrID, addr->endTxYmd_, txLog2->ymd_,
                             txLog2->toString().c_str());
      }
    }
    assert(addr->totalReceived_ - addr->totalSent_ + balanceDiff >= 0);

    //
    // 插入当前记录
    //
    sql = Strings::Format("INSERT INTO `address_txs_%d` (`address_id`, `tx_id`, `tx_height`,"
                          " `total_received`, `balance_diff`, `balance_final`, `idx`, `ymd`,"
                          " `prev_ymd`, `prev_tx_id`, `next_ymd`, `next_tx_id`, `created_at`)"
                          " VALUES (%lld, %lld, %d, %lld, %lld, %lld, %lld, %d, "
                          "         %d, %lld, 0, 0, '%s') ",
                          tableIdx_AddrTxs(txLog2->ymd_), addrID, txLog2->txId_, txLog2->blkHeight_,
                          addr->totalReceived_ + (balanceDiff > 0 ? balanceDiff : 0),
                          balanceDiff,
                          addr->totalReceived_ - addr->totalSent_ + balanceDiff,
                          addr->txCount_+ 1, txLog2->ymd_,
                          addr->endTxYmd_, addr->endTxId_, date("%F %T").c_str());
    db.updateOrThrowEx(sql, 1);

    //
    // 更新地址信息
    //
    string sqlBegin = "";  // 是否更新 `begin_tx_ymd`/`begin_tx_id`
    if (addr->beginTxYmd_ == 0) {
      assert(addr->beginTxId_  == 0);
      sqlBegin = Strings::Format("`begin_tx_ymd`=%d, `begin_tx_id`=%lld,",
                                 txLog2->ymd_, txLog2->txId_);
      addr->beginTxId_  = txLog2->txId_;
      addr->beginTxYmd_ = txLog2->ymd_;
    }
    const int64_t balanceReceived = (balanceDiff > 0 ? balanceDiff : 0);
    const int64_t balanceSent     = (balanceDiff < 0 ? balanceDiff * -1 : 0);
    sql = Strings::Format("UPDATE `%s` SET `tx_count`=`tx_count`+1, "
                          " `total_received` = `total_received` + %lld,"
                          " `total_sent`     = `total_sent`     + %lld,"
                          " `unconfirmed_received` = `unconfirmed_received` + %lld,"
                          " `unconfirmed_sent`     = `unconfirmed_sent`     + %lld,"
                          " `end_tx_ymd`=%d, `end_tx_id`=%lld, %s "
                          " `updated_at`='%s' WHERE `id`=%lld ",
                          addrTableName.c_str(),
                          balanceReceived, balanceSent, balanceReceived, balanceSent,
                          txLog2->ymd_, txLog2->txId_, sqlBegin.c_str(),
                          date("%F %T").c_str(), addrID);
    db.updateOrThrowEx(sql, 1);

    addr->totalReceived_ += balanceReceived;
    addr->totalSent_     += balanceSent;
    addr->unconfirmedReceived_ += balanceReceived;
    addr->unconfirmedSent_     += balanceSent;
    addr->endTxId_  = txLog2->txId_;
    addr->endTxYmd_ = txLog2->ymd_;
    addr->txCount_++;

  } /* /for */
}

// 插入交易
static
void _accpetTx_insertTx(MySQLConnection &db, class TxLog2 *txLog2, int64_t valueIn) {
  //
  // (`tx_id`, `hash`, `height`, `is_coinbase`, `version`, `lock_time`,
  //  `size`, `fee`, `total_in_value`, `total_out_value`,
  // `inputs_count`, `outputs_count`, `created_at`)
  //
  const string tName = Strings::Format("txs_%04d", tableIdx_Tx(txLog2->txId_));
  const CTransaction &tx = txLog2->tx_;  // alias
  string sql;

  int64_t fee = 0;
  const int64_t valueOut = txLog2->tx_.GetValueOut();
  if (txLog2->tx_.IsCoinBase()) {
    // coinbase的fee为 block rewards
    fee = valueOut - GetBlockValue(txLog2->blkHeight_, 0);
  } else {
    fee = valueIn - valueOut;
  }

  sql = Strings::Format("INSERT INTO `%s`(`tx_id`, `hash`, `height`, `is_coinbase`,"
                        " `version`, `lock_time`, `size`, `fee`, `total_in_value`, "
                        " `total_out_value`, `inputs_count`, `outputs_count`, `created_at`)"
                        " VALUES (%lld, '%s', %d, %u, %d, %d, %u, %d, %lld, %lld, %lld, %d, %d, '%s') ",
                        tName.c_str(),
                        // `tx_id`, `hash`, `height`
                        txLog2->txId_, txLog2->txHash_.ToString().c_str(),
                        txLog2->blkHeight_,
                        // `is_coinbase`, `version`, `lock_time`
                        txLog2->tx_.IsCoinBase() ? 1 : 0, tx.nVersion, tx.nLockTime,
                        // `size`, `fee`, `total_in_value`, `total_out_value`
                        txLog2->txHex_.length()/2, fee, valueIn, valueOut,
                        // `inputs_count`, `outputs_count`, `created_at`
                        tx.vin.size(), tx.vout.size(), date("%F %T").c_str());
  db.updateOrThrowEx(sql, 1);
}


void Parser::addUnconfirmedTxPool(class TxLog2 *txLog2) {
  string sql;
  const string nowStr = date("%F %T");

  //
  // 0_unconfirmed_txs
  //
  sql = Strings::Format("INSERT INTO `0_unconfirmed_txs` (`position`,`block_id`,`tx_hash`,`size`,`created_at`)"
                        " VALUES ("
                        " (SELECT IFNULL(MAX(`position`), -1) + 1 FROM `0_unconfirmed_txs` as t1), "
                        " %lld, '%s',%d,'%s') ",
                        txLog2->blkId_, txLog2->txHash_.ToString().c_str(),
                        (int32_t)(txLog2->txHex_.length() / 2),
                        nowStr.c_str());
  dbExplorer_.updateOrThrowEx(sql, 1);

  //
  // 0_explorer_meta
  //
  // jiexi.unconfirmed_txs.count
  //
  unconfirmedTxsCount_++;
  sql = Strings::Format("UPDATE `0_explorer_meta` SET `value` = %d, `updated_at`='%s' "
                        " WHERE `key` = 'jiexi.unconfirmed_txs.count' ",
                        unconfirmedTxsCount_, nowStr.c_str());
  dbExplorer_.updateOrThrowEx(sql, 1);

  //
  // jiexi.unconfirmed_txs.size
  //
  unconfirmedTxsSize_ += txLog2->txHex_.length() / 2;
  sql = Strings::Format("UPDATE `0_explorer_meta` SET `value` = %d, `updated_at`='%s' "
                        " WHERE `key` = 'jiexi.unconfirmed_txs.size' ",
                        unconfirmedTxsSize_, nowStr.c_str());
  dbExplorer_.updateOrThrowEx(sql, 1);
}

void Parser::removeUnconfirmedTxPool(class TxLog2 *txLog2) {
  string sql;
  const string nowStr = date("%F %T");
  sql = Strings::Format("DELETE FROM `0_unconfirmed_txs` WHERE `tx_hash`='%s'",
                        txLog2->txHash_.ToString().c_str());
  dbExplorer_.updateOrThrowEx(sql, 1);

  //
  // 0_explorer_meta
  //
  // jiexi.unconfirmed_txs.count
  //
  unconfirmedTxsCount_--;
  assert(unconfirmedTxsCount_ >= 0);
  sql = Strings::Format("UPDATE `0_explorer_meta` SET `value` = %d, `updated_at`='%s' "
                        " WHERE `key` = 'jiexi.unconfirmed_txs.count' ",
                        unconfirmedTxsCount_, nowStr.c_str());
  dbExplorer_.updateOrThrowEx(sql, 1);

  //
  // jiexi.unconfirmed_txs.size
  //
  unconfirmedTxsSize_ -= txLog2->txHex_.length() / 2;
  assert(unconfirmedTxsSize_ >= 0);
  sql = Strings::Format("UPDATE `0_explorer_meta` SET `value` = %d, `updated_at`='%s' "
                        " WHERE `key` = 'jiexi.unconfirmed_txs.size' ",
                        unconfirmedTxsSize_, nowStr.c_str());
  dbExplorer_.updateOrThrowEx(sql, 1);
}

// 交换相邻节点，当前节点前移
void Parser::_switchAddressTxNode(AddressTxNode *prev, AddressTxNode *curr) {
  //
  // 顺序调整示意：   1, 2(prev), 3(curr), 4   ->   1, 3, 2, 4
  // 其中：2，3必须存在，1和4均不一定存在
  //
  assert(curr->prevTxId_ != 0);
  string sql;

  // 变更后续节点, 节点4: 前向节点指向2。涉及指向，不涉及金额变化
  if (curr->nextTxId_ > 0) {
    // 涉及指向，不涉及金额变化
    sql = Strings::Format("UPDATE `address_txs_%d` SET `prev_ymd`=%d, `prev_tx_id`=%lld "
                          " WHERE `address_id`=%lld AND `tx_id`=%lld ",
                          tableIdx_AddrTxs(curr->nextYmd_),
                          curr->prevYmd_, curr->prevTxId_,
                          curr->addressId_, curr->nextTxId_);
    dbExplorer_.updateOrThrowEx(sql , 1);
  }

  // 变更后续节点, 节点1: 后向节点指向3。涉及指向，不涉及金额变化
  if (prev->prevTxId_ > 0) {
    sql = Strings::Format("UPDATE `address_txs_%d` SET `next_ymd`=%d, `next_tx_id`=%lld "
                          " WHERE `address_id`=%lld AND `tx_id`=%lld ",
                          tableIdx_AddrTxs(prev->prevYmd_), curr->ymd_, curr->txId_,
                          curr->addressId_, prev->prevTxId_);
    dbExplorer_.updateOrThrowEx(sql , 1);
  }

  // 变更前向节点，节点2: 指向 & 金额
  sql = Strings::Format("UPDATE `address_txs_%d` "
                        " SET `prev_ymd`=%d, `prev_tx_id`=%lld, "
                        "     `next_ymd`=%d, `next_tx_id`=%lld, "
                        " `balance_final`  = `balance_final`  + %lld, "
                        " `total_received` = `total_received` + %lld, "
                        " `idx` = `idx` + 1 "
                        " WHERE `address_id`=%lld AND `tx_id`=%lld ",
                        tableIdx_AddrTxs(curr->prevYmd_),
                        curr->ymd_, curr->txId_,
                        curr->nextYmd_, curr->nextTxId_,
                        curr->balanceDiff_,
                        (curr->balanceDiff_ > 0 ? curr->balanceDiff_ : 0),
                        curr->addressId_, curr->prevTxId_);
  dbExplorer_.updateOrThrowEx(sql , 1);

  // 变更当前节点，节点3: 指向 & 金额
  sql = Strings::Format("UPDATE `address_txs_%d` "
                        " SET `prev_ymd`=%d, `prev_tx_id`=%lld, "
                        "     `next_ymd`=%d, `next_tx_id`=%lld, "
                        " `balance_final`  = `balance_final`  + %lld, "
                        " `total_received` = `total_received` + %lld, "
                        " `idx` = `idx` - 1 "
                        " WHERE `address_id`=%lld AND `tx_id`=%lld ",
                        tableIdx_AddrTxs(curr->ymd_),
                        prev->prevYmd_, prev->prevTxId_,
                        prev->ymd_, prev->txId_,
                        prev->balanceDiff_ * -1,
                        (prev->balanceDiff_ > 0 ? prev->balanceDiff_ * -1 : 0),
                        curr->addressId_, curr->txId_);
  dbExplorer_.updateOrThrowEx(sql , 1);
}

// 交换相邻节点，当前节点前移
void Parser::moveForwardAddressTxNode(const LastestAddressInfo *addr,
                                      AddressTxNode *curr) {
  //
  // 顺序调整示意：   1, 2(prevNode), 3(curr), 4   ->   1, 3, 2, 4
  //
  assert(curr->prevTxId_ != 0);
  string sql;
  AddressTxNode prevNode;
  _getAddressTxNode(curr->prevTxId_, addr, &prevNode);

  _switchAddressTxNode(&prevNode, curr);
}

// 交换相邻节点，当前节点后移
void Parser::moveBackwardAddressTxNode(const LastestAddressInfo *addr,
                                       AddressTxNode *curr) {
  //
  // 顺序调整示意：   1, 2(curr), 3(nextNode), 4   ->   1, 3, 2, 4
  //
  assert(curr->nextTxId_ != 0);
  string sql;
  AddressTxNode nextNode;
  _getAddressTxNode(curr->nextTxId_, addr, &nextNode);

  _switchAddressTxNode(curr, &nextNode);
}

// 接收一个新的交易
void Parser::acceptTx(class TxLog2 *txLog2) {
  assert(txLog2->blkHeight_ == -1);

  // 硬编码特殊交易处理
  //
  // 1. tx hash: d5d27987d2a3dfc724e359870c6644b40e497bdc0589a033220fe15429d88599
  // 该交易在两个不同的高度块(91812, 91842)中出现过
  // 91842块中有且仅有这一个交易
  //
  // 2. tx hash: e3bf3d07d4b0375638d5f1db5255fe07ba2c4cb067cd81b84ee974b6585fb468
  // 该交易在两个不同的高度块(91722, 91880)中出现过
  if ((txLog2->blkHeight_ == 91842 &&
       txLog2->txHash_ == uint256("d5d27987d2a3dfc724e359870c6644b40e497bdc0589a033220fe15429d88599")) ||
      (txLog2->blkHeight_ == 91880 &&
       txLog2->txHash_ == uint256("e3bf3d07d4b0375638d5f1db5255fe07ba2c4cb067cd81b84ee974b6585fb468"))) {
    LOG_WARN("ignore tx, height: %d, hash: %s",
             txLog2->blkHeight_, txLog2->txHash_.ToString().c_str());
    return;
  }

  // 交易的输入之和，遍历交易后才能得出
  int64_t valueIn = 0;

  // 同一个地址只能产生一条交易记录: address <-> tx <-> balance_diff
  map<int64_t/* addrID */, int64_t/*  balance diff */> addressBalance;

  // 处理inputs
  _accpetTx_insertTxInputs(dbExplorer_, cache_, txLog2, addressBalance, valueIn);

  // 处理outputs
  _accpetTx_insertTxOutputs(dbExplorer_, cache_, txLog2, addressBalance);

  // 缓存 addressBalance, 减少 confirm tx 操作时查找关联地址变更记录的开销
  assert(addressBalanceCache_.find(txLog2->txHash_) == addressBalanceCache_.end());
  addressBalanceCache_[txLog2->txHash_] = addressBalance;

  // 变更地址相关信息
  _accpetTx_insertAddressTxs(dbExplorer_, txLog2, addressBalance);

  // 插入交易tx
  _accpetTx_insertTx(dbExplorer_, txLog2, valueIn);

  // 处理未确认计数器和记录
  addUnconfirmedTxPool(txLog2);
}


static void _getTxAddressBalance(class TxLog2 *txLog2,
                                map<int64_t, int64_t> *addressBalance) {
  // TODO
}

int32_t prevYmd(const int32_t ymd) {
  // 转为时间戳
  const string s = Strings::Format("%d", ymd) + " 00:00:00";
  const time_t t = str2time(s.c_str(), "%Y%m%d %H:%M:%S");
  // 向前步进一天
  return atoi(date("%Y%m%d", t - 86400).c_str());
}

void Parser::_getAddressTxNode(const int64_t txId,
                               const LastestAddressInfo *addr, AddressTxNode *node) {
  // 这里涉及的Node通常是最近的，故从最后一个ymd向前找
  int32_t ymd = addr->endTxYmd_;
  string sql;
  MySQLResult res;
  char **row = nullptr;

  node->reset();

  while (ymd >= addr->beginTxYmd_) {
    const int32_t currTableIdx = tableIdx_AddrTxs(ymd);
    sql = Strings::Format("SELECT `tx_height`, `total_received`, `balance_diff`, "
                          "  `balance_final`, `idx`, `prev_ymd`, "
                          "  `next_ymd`, `prev_tx_id`, `next_tx_id` "
                          " FROM `address_txs_%d` "
                          " WHERE `address_id`=%lld AND `tx_id`=%lld ",
                          currTableIdx, addr->addrId_, txId);
    dbExplorer_.query(sql, res);

    // 没找到，则向前找一个表查找，按天向前移动，直至发生更换表索引
    if (res.numRows() == 0) {
      while (1) {
      	ymd = prevYmd(ymd);
        if (ymd < addr->beginTxYmd_) {
          break;
        }
        if (tableIdx_AddrTxs(ymd) == currTableIdx) {
          continue;
        }
      }
      continue;
    }

    row = res.nextRow();

    node->ymd_ = ymd;
    node->txHeight_  = atoi(row[0]);
    node->addressId_ = addr->addrId_;
    node->txId_      = txId;
    node->totalReceived_ = atoi64(row[1]);
    node->balanceDiff_   = atoi64(row[2]);
    node->balanceFinal_  = atoi64(row[3]);
    node->idx_      = atoi64(row[4]);
    node->prevYmd_  = atoi(row[5]);
    node->nextYmd_  = atoi(row[6]);
    node->prevTxId_ = atoi64(row[7]);
    node->nextTxId_ = atoi64(row[8]);

    break;
  } /* /while */

  if (node->ymd_ == 0) {  // 必须获取到数据
    THROW_EXCEPTION_DBEX("can't find address_tx, address_id: %lld, tx_id: %lld",
                         addr->addrId_, txId);
  }
}

// confirm tx (address node)
void Parser::_confirmAddressTxNode(AddressTxNode *node, LastestAddressInfo *addr,
                                   const int32_t height) {
  string sql;

  //
  // 更新 address_txs_<yyyymm>.tx_height
  //
  sql = Strings::Format("UPDATE `address_txs_%d` SET `tx_height`=%d "
                        " WHERE `address_id`=%lld AND `tx_id`=%lld ",
                        tableIdx_AddrTxs(node->ymd_), height,
                        node->addressId_, node->idx_);
  dbExplorer_.updateOrThrowEx(sql, 1);

  //
  // 变更地址未确认额度等信息
  //
  const int64_t received = (node->balanceDiff_ > 0 ? node->balanceDiff_ : 0);
  const int64_t sent     = (node->balanceDiff_ < 0 ? node->balanceDiff_ * -1 : 0);

  sql = Strings::Format("UPDATE `addresses_%04d` SET "
                        " `unconfirmed_received` = `unconfirmed_received` - %lld,"
                        " `unconfirmed_sent`     = `unconfirmed_sent`     - %lld,"
                        " `last_confirmed_tx_ymd`=%d, `last_confirmed_tx_id`=%lld, "
                        " `updated_at`='%s' WHERE `id`=%lld ",
                        tableIdx_Addr(addr->addrId_),
                        received, sent, node->ymd_, node->txId_,
                        date("%F %T").c_str(), addr->addrId_);
  dbExplorer_.updateOrThrowEx(sql, 1);

  addr->unconfirmedReceived_ -= received;
  addr->unconfirmedSent_     -= sent;
  addr->lastConfirmedTxId_  = node->txId_;
  addr->lastConfirmedTxYmd_ = node->ymd_;
}

// 更新后续节点的 ymd
// 该节点之后的（包含本节点）全部移动至 ymd 表中去
void Parser::_changeYmdAddressTxNode_R(const LastestAddressInfo *addr,
                                       const int32_t targetYmd, AddressTxNode *node) {
  string sql;

  if (tableIdx_AddrTxs(targetYmd) == tableIdx_AddrTxs(node->ymd_)) {
    return;
  }

  // 插入记录至新表
  const string fields = "`address_id`, `tx_id`, `tx_height`, `total_received`, "
  "`balance_diff`, `balance_final`, `idx`, `ymd`, `prev_ymd`, "
  "`prev_tx_id`, `next_ymd`, `next_tx_id`, `created_at`";
  sql = Strings::Format("INSERT INTO `address_txs_%d` (%s) "
                        " SELECT %s FROM `address_txs_%d` "
                        " WHERE `address_id`=%lld AND `tx_id`=%lld ",
                        tableIdx_AddrTxs(targetYmd), fields.c_str(),
                        tableIdx_AddrTxs(node->ymd_), fields.c_str(),
                        node->addressId_, node->txId_);
  dbExplorer_.updateOrThrowEx(sql, 1);

  // 删除旧记录
  sql = Strings::Format("DELETE FROM `address_txs_%d` "
                        " WHERE `address_id`=%lld AND `tx_id`=%lld ",
                        tableIdx_AddrTxs(node->ymd_), node->addressId_, node->txId_);
  dbExplorer_.updateOrThrowEx(sql, 1);

  // 调整后续记录
  if (node->nextTxId_ && tableIdx_AddrTxs(node->nextYmd_) < tableIdx_AddrTxs(targetYmd)) {
    AddressTxNode nextNode;
    _getAddressTxNode(node->nextTxId_, addr, &nextNode);

    // -R
    _changeYmdAddressTxNode_R(addr, targetYmd, &nextNode);
  }
}

// 确认一个交易
void Parser::confirmTx(class TxLog2 *txLog2) {
  //
  // confirm: 变更为确认（设定块高度值等），若不是紧接着上个已确认的交易，则调整交易链。
  //          若该交易涉及日期变更（从上月末变更为下月初），则需要移动其后所有的tx至新日期表中
  //

  // 拿到关联地址的变迁记录，可能需要调整交易链
  map<int64_t/* addrID */, int64_t/*  balance diff */> addressBalance;
  if (addressBalanceCache_.count(txLog2->txHash_) == 0) {
    _getTxAddressBalance(txLog2, &addressBalance);
    addressBalanceCache_[txLog2->txHash_] = addressBalance;
  }

  for (auto &it : addressBalance) {
    const int64_t addrID       = it.first;

    // 获取地址信息
    LastestAddressInfo *addr = _getAddressInfo(dbExplorer_, addrID);

    // 当前节点
    AddressTxNode currNode;

    //
    // 保障确认的节点在上一个确认节点的后方
    //
    while (1) {
      _getAddressTxNode(txLog2->txId_, addr, &currNode);

      // 涉及移动节点的情形: 即将确认的节点不在上一个确认节点的后方，即中间掺杂了其他交易
      // 若前面无节点，两个值均为零
      if (addr->lastConfirmedTxId_ == currNode.prevTxId_) {
        break;
      }
      // 向前移动本节点
      moveForwardAddressTxNode(addr, &currNode);
    };
    // 最后一个确认的ymd必然小于等于当前即将确认的ymd
    assert(addr->lastConfirmedTxYmd_ <= currNode.ymd_);

    //
    // ymd 不一致，需要更新数据至目标表中
    //
    if (currNode.ymd_ > txLog2->ymd_) {
      // 不能应存在此情形： accept 的时间大于 confirm 的时间
      THROW_EXCEPTION_DBEX("currNode.ymd_: %d > txLog2->ymd_: %d",
                           currNode.ymd_, txLog2->ymd_);
    }
    // 需要移动的情形：上一个确认在7月份，未确认的也在7月份，此时确认7月份的未确认交易至8月份
    // 需要移动该节点之后的（包含本节点）至 txLog2->ymd_ 所在的表里
    if (currNode.ymd_ < txLog2->ymd_) {
      _changeYmdAddressTxNode_R(addr, txLog2->ymd_, &currNode);
      _getAddressTxNode(txLog2->txId_, addr, &currNode);
    }

    //
    // 确认
    //
    _confirmAddressTxNode(&currNode, addr, txLog2->blkHeight_);
  } /* /for */

  // TODO: 变更 table.txs_xxxx

  // 处理未确认计数器和记录
  removeUnconfirmedTxPool(txLog2);
}

// unconfirm tx (address node)
void Parser::_unconfirmAddressTxNode(AddressTxNode *node, LastestAddressInfo *addr) {
  string sql;

  //
  // 更新 address_txs_<yyyymm>.tx_height
  //
  sql = Strings::Format("UPDATE `address_txs_%d` SET `tx_height`=0 "
                        " WHERE `address_id`=%lld AND `tx_id`=%lld ",
                        tableIdx_AddrTxs(node->ymd_),
                        node->addressId_, node->idx_);
  dbExplorer_.updateOrThrowEx(sql, 1);

  //
  // 变更地址未确认额度等信息
  //
  const int64_t received = (node->balanceDiff_ > 0 ? node->balanceDiff_ : 0);
  const int64_t sent     = (node->balanceDiff_ < 0 ? node->balanceDiff_ * -1 : 0);

  AddressTxNode prevNode;
  if (node->prevTxId_) {
    _getAddressTxNode(node->prevTxId_, addr, &prevNode);
  }

  sql = Strings::Format("UPDATE `addresses_%04d` SET "
                        " `unconfirmed_received` = `unconfirmed_received` + %lld,"
                        " `unconfirmed_sent`     = `unconfirmed_sent`     + %lld,"
                        " `last_confirmed_tx_ymd`=%d, `last_confirmed_tx_id`=%lld, "
                        " `updated_at`='%s' WHERE `id`=%lld ",
                        tableIdx_Addr(addr->addrId_),
                        received, sent,
                        (node->prevTxId_ ? prevNode.ymd_  : 0),
                        (node->prevTxId_ ? prevNode.txId_ : 0),
                        date("%F %T").c_str(), addr->addrId_);
  dbExplorer_.updateOrThrowEx(sql, 1);

  addr->unconfirmedReceived_ -= received;
  addr->unconfirmedSent_     -= sent;
  addr->lastConfirmedTxId_  = (node->prevTxId_ ? prevNode.txId_ : 0);
  addr->lastConfirmedTxYmd_ = (node->prevTxId_ ? prevNode.ymd_  : 0);
}

// 取消交易的确认
void Parser::unconfirmTx(class TxLog2 *txLog2) {
  //
  // unconfirm: 解除最后一个已确认的交易（重置块高度零等），必然是最近确认交易，不涉及调整交易链
  //

  // 拿到关联地址的变迁记录
  map<int64_t/* addrID */, int64_t/*  balance diff */> addressBalance;
  if (addressBalanceCache_.count(txLog2->txHash_) == 0) {
    _getTxAddressBalance(txLog2, &addressBalance);
    addressBalanceCache_[txLog2->txHash_] = addressBalance;
  }

  for (auto &it : addressBalance) {
    const int64_t addrID       = it.first;
//    const int64_t balanceDiff  = it.second;

    // 获取地址信息
    LastestAddressInfo *addr = _getAddressInfo(dbExplorer_, addrID);

    // 当前节点
    AddressTxNode currNode;
    _getAddressTxNode(txLog2->txId_, addr, &currNode);

    // 反确认
    _unconfirmAddressTxNode(&currNode, addr);
  } /* /for */
  
  // TODO: 变更 table.txs_xxxx

  // 处理未确认计数器和记录
  addUnconfirmedTxPool(txLog2);
}

// 回滚一个块操作
void Parser::rejectBlock(TxLog2 *txLog2) {
  string sql;

  // 获取块Raw Hex
  string blkRawHex;
  _getBlockRawHexByBlockId(txLog2->blkId_, dbExplorer_, &blkRawHex);

  // 解码Raw Block
  CBlock blk;
  if (!DecodeHexBlk(blk, blkRawHex)) {
    THROW_EXCEPTION_DBEX("decode block hex failure, hex: %s", blkRawHex.c_str());
  }

  const CBlockHeader header  = blk.GetBlockHeader();  // alias
  const string prevBlockHash = header.hashPrevBlock.ToString();

  // 移除前向block的next指向
  sql = Strings::Format("UPDATE `0_blocks` SET `next_block_id`=0, `next_block_hash`=''"
                        " WHERE `hash` = '%s' ", prevBlockHash.c_str());
  dbExplorer_.updateOrThrowEx(sql, 1);

  // table.0_blocks
  sql = Strings::Format("DELETE FROM `0_blocks` WHERE `hash`='%s'",
                        header.GetHash().ToString().c_str());
  dbExplorer_.updateOrThrowEx(sql, 1);

  // table.block_txs_xxxx
  sql = Strings::Format("DELETE FROM `block_txs_%04d` WHERE `block_id`=%lld",
                        tableIdx_BlockTxs(txLog2->blkId_), txLog2->blkId_);
  dbExplorer_.updateOrThrowEx(sql, (int32_t)blk.vtx.size());

  if (cacheEnable_) {
    // http://twiki.bitmain.com/bin/view/Main/SSDB-Cache
    cache_->insertKV(Strings::Format("blkh_%d", txLog2->blkHeight_));
    cache_->insertKV(Strings::Format("blk_%s",
                                     header.GetHash().ToString().c_str()));
  }
}

// 回滚，重新插入未花费记录
void _unremoveUnspentOutputs(MySQLConnection &db, CacheManager *cache,
                             const int32_t blockHeight,
                             const int64_t txId, const int32_t position,
                             map<int64_t, int64_t> &addressBalance,
                             const int32_t ymd) {
  MySQLResult res;
  string sql;
  string tableName;
  char **row;
  int n;

  // 获取相关output信息
  tableName = Strings::Format("tx_outputs_%04d", txId % 100);
  sql = Strings::Format("SELECT `address_ids`,`value`,`address` FROM `%s` "
                        " WHERE `tx_id`=%lld AND `position`=%d",
                        tableName.c_str(), txId, position);
  db.query(sql, res);
  row = res.nextRow();
  assert(res.numRows() == 1);

  // 获取地址
  const string s      = string(row[0]);
  const int64_t value = atoi64(row[1]);
  const string ids    = string(row[2]);

  vector<string> addressIdsStrVec = split(s, '|');
  n = -1;
  for (auto &addrIdStr : addressIdsStrVec) {
    n++;
    const int64_t addrId = atoi64(addrIdStr.c_str());
    addressBalance[addrId] += -1 * value;

    // 恢复每一个输出地址的 address_unspent_outputs 记录
    sql = Strings::Format("INSERT INTO `address_unspent_outputs_%04d`"
                          " (`address_id`, `tx_id`, `position`, `position2`, `block_height`, `value`, `created_at`)"
                          " VALUES (%lld, %lld, %d, %d, %d, %lld, '%s') ",
                          addrId % 10, addrId, txId, position, n, blockHeight,
                          value, date("%F %T").c_str());
    db.updateOrThrowEx(sql, 1);
  }

  // 缓存
  if (cache) {
    auto addrStrVec = split(ids, '|');
    for (auto &singleAddrStr : addrStrVec) {
      cache->insertKV(Strings::Format("addr_%s", singleAddrStr.c_str()));
      cache->insertHashSet(singleAddrStr, Strings::Format("address_txs_%d",
                                                          tableIdx_AddrTxs(ymd)));
    }
  }
}

// 回滚交易 inputs
static
void _rejectTxInputs(MySQLConnection &db, CacheManager *cache,
                     class TxLog2 *txLog2,
                     map<int64_t, int64_t> &addressBalance) {
  const CTransaction &tx = txLog2->tx_;
  const int32_t blockHeight = txLog2->blkHeight_;
  const int64_t txId = txLog2->txId_;
  const int32_t ymd = 0;  // TODO

  string sql;
  for (auto &in : tx.vin) {
    // 非 coinbase 无需处理前向交易
    if (tx.IsCoinBase()) { continue; }

    uint256 prevHash = in.prevout.hash;
    int64_t prevTxId = txHash2Id(db, prevHash);
    int32_t prevPos  = (int32_t)in.prevout.n;

    // 将前向交易标记为未花费
    sql = Strings::Format("UPDATE `tx_outputs_%04d` SET "
                          " `spent_tx_id`=0, `spent_position`=-1"
                          " WHERE `tx_id`=%lld AND `position`=%d "
                          " AND `spent_tx_id`<>0 AND `spent_position`<>-1 ",
                          prevTxId % 100, prevTxId, prevPos);
    db.updateOrThrowEx(sql, 1);

    // 重新插入 address_unspent_outputs_xxxx 相关记录
    _unremoveUnspentOutputs(db, cache, blockHeight, prevTxId, prevPos,
                            addressBalance, ymd);

    if (cache != nullptr) {
      // http://twiki.bitmain.com/bin/view/Main/SSDB-Cache
      // 遍历inputs所关联tx，全部删除： txid_{id}, tx_{hash}
      cache->insertKV(Strings::Format("tx_%s", prevHash.ToString().c_str()));
    }
  } /* /for */

  // 删除 table.tx_inputs_xxxx 记录， coinbase tx 也有 tx_inputs_xxxx 记录
  sql = Strings::Format("DELETE FROM `tx_inputs_%04d` WHERE `tx_id`=%lld ",
                        txId % 100, txId);
  db.updateOrThrowEx(sql, (int32_t)tx.vin.size());
}

// 回滚交易 outputs
static
void _rejectTxOutputs(MySQLConnection &db, CacheManager *cache,
                      class TxLog2 *txLog2,
                      map<int64_t, int64_t> &addressBalance) {
  const CTransaction &tx = txLog2->tx_;
  const int64_t txId = txLog2->txId_;
  const int32_t ymd = 0;  // TODO

  int n;
  const string now = date("%F %T");
  set<string> allAddresss;
  string sql;

  // 提取涉及到的所有地址
  for (auto &out : tx.vout) {
    txnouttype type;
    vector<CTxDestination> addresses;
    int nRequired;
    if (!ExtractDestinations(out.scriptPubKey, type, addresses, nRequired)) {
      LOG_WARN("extract destinations failure, txId: %lld, hash: %s",
               txId, tx.GetHash().ToString().c_str());
      continue;
    }
    for (auto &addr : addresses) {  // multiSig 可能由多个输出地址
      allAddresss.insert(CBitcoinAddress(addr).ToString());
    }
  }
  // 拿到所有地址的id
  map<string, int64_t> addrMap;
  GetAddressIds(db, allAddresss, addrMap);

  if (cache != nullptr) {
    for (auto &singleAddrStr : allAddresss) {
      cache->insertKV(Strings::Format("addr_%s", singleAddrStr.c_str()));
      cache->insertHashSet(singleAddrStr, Strings::Format("address_txs_%d",
                                                    tableIdx_AddrTxs(ymd)));
    }
  }

  // 处理输出
  // (`address_id`, `tx_id`, `position`, `position2`, `block_height`, `value`, `created_at`)
  n = -1;
  for (auto &out : tx.vout) {
    n++;
    txnouttype type;
    vector<CTxDestination> addresses;
    int nRequired;
    if (!ExtractDestinations(out.scriptPubKey, type, addresses, nRequired)) {
      LOG_WARN("extract destinations failure, txId: %lld, hash: %s, position: %d",
               txId, tx.GetHash().ToString().c_str(), n);
    }

    // multiSig 可能由多个输出地址: https://en.bitcoin.it/wiki/BIP_0011
    int i = -1;
    for (auto &addr : addresses) {
      i++;
      const string addrStr = CBitcoinAddress(addr).ToString();
      const int64_t addrId = addrMap[addrStr];
      addressBalance[addrId] += out.nValue;

      // 删除： address_unspent_outputs 记录
      sql = Strings::Format("DELETE FROM `address_unspent_outputs_%04d` "
                            " WHERE `address_id`=%lld AND `tx_id`=%lld "
                            " AND `position`=%d AND `position2`=%d ",
                            tableIdx_AddrUnspentOutput(addrId),
                            addrId, txId, n, i);
      db.updateOrThrowEx(sql, 1);
    }
  }

  // delete table.tx_outputs_xxxx
  sql = Strings::Format("DELETE FROM `tx_outputs_%04d` WHERE `tx_id`=%lld",
                        txId % 100, txId);
  db.updateOrThrowEx(sql, (int32_t)tx.vout.size());
}


// 移除地址交易节点 (含地址信息变更)
void Parser::_removeAddressTxNode(LastestAddressInfo *addr, AddressTxNode *node) {
  string sql;

  // 设置倒数第二条记录 next 记录为空, 如果存在的话
  if (node->prevYmd_ != 0) {
    assert(node->prevTxId_ != 0);

    sql = Strings::Format("UPDATE `address_txs_%d` SET `next_ymd`=0, `next_tx_id`=0 "
                          " WHERE `address_id`=%lld AND `tx_id`=%lld ",
                          tableIdx_AddrTxs(node->prevYmd_),
                          addr->addrId_, node->prevTxId_);
    dbExplorer_.updateOrThrowEx(sql, 1);
  }

  // 删除最后一个交易节点记录
  sql = Strings::Format("DELETE FROM `address_txs_%d` WHERE `address_id`=%lld AND `tx_id`=%lld ",
                        tableIdx_AddrTxs(node->ymd_), addr->addrId_, node->txId_);
  dbExplorer_.updateOrThrowEx(sql, 1);

  // 更新地址信息
  const int64_t received = (node->balanceDiff_ > 0 ? node->balanceDiff_ : 0);
  const int64_t sent     = (node->balanceDiff_ < 0 ? node->balanceDiff_ * -1 : 0);

  // 移除的节点必然是未确认的，需要 unconfirmed_xxxx 余额变更
  sql = Strings::Format("UPDATE `addresses_%04d` SET `tx_count`=`tx_count`-1, "
                        " `total_received` = `total_received` - %lld,"
                        " `total_sent`     = `total_sent`     - %lld,"
                        " `unconfirmed_received` = `unconfirmed_received` - %lld,"
                        " `unconfirmed_sent`     = `unconfirmed_sent`     - %lld,"
                        " `end_tx_ymd`=%d, `end_tx_id`=%lld, %s `updated_at`='%s' "
                        " WHERE `id`=%lld ",
                        tableIdx_Addr(addr->addrId_),
                        received, sent, received, sent,
                        node->prevYmd_, node->prevTxId_,
                        // 没有倒数第二条，重置起始位置为空
                        node->prevYmd_ == 0 ? "`begin_tx_id`=0,`begin_tx_ymd`=0," : "",
                        date("%F %T").c_str(), addr->addrId_);
  dbExplorer_.updateOrThrowEx(sql, 1);

  addr->txCount_--;
  addr->totalReceived_ -= received;
  addr->totalSent_     -= sent;
  addr->unconfirmedReceived_ -= received;
  addr->unconfirmedSent_     -= sent;
  addr->endTxYmd_ = node->prevYmd_;
  addr->endTxId_  = node->prevTxId_;
}


// 回滚：变更地址&地址对应交易记录
void Parser::_rejectAddressTxs(class TxLog2 *txLog2,
                               const map<int64_t, int64_t> &addressBalance) {
  for (auto &it : addressBalance) {
    const int64_t addrID      = it.first;
    //
    // 当前要reject的记录不是最后一条记录：需要移动节点
    //
    LastestAddressInfo *addr = _getAddressInfo(dbExplorer_, addrID);
    AddressTxNode currNode;  // 当前节点

    do {
      _getAddressTxNode(txLog2->txId_, addr, &currNode);

      // 涉及移动节点的情形: 当前节点并非最后一个节点
      if (addr->endTxId_ == currNode.txId_) {
        break;
      }
      // 向后移动本节点
      moveBackwardAddressTxNode(addr, &currNode);
    } while (1);
    assert(addr->endTxId_ == currNode.txId_);

    // 移除本节点 (含地址信息变更)
    _removeAddressTxNode(addr, &currNode);
  } /* /for */
}


static
void _rejectTx(MySQLConnection &db, CacheManager *cache, class TxLog2 *txLog2) {
  string sql = Strings::Format("DELETE FROM `txs_%04d` WHERE `tx_id`=%lld ",
                               tableIdx_Tx(txLog2->txId_), txLog2->txId_);
  db.updateOrThrowEx(sql, 1);

  if (cache != nullptr) {
    // http://twiki.bitmain.com/bin/view/Main/SSDB-Cache
    // 删除自身: tx_{hash}
    cache->insertKV(Strings::Format("tx_%s", txLog2->txHash_.ToString().c_str()));
  }
}


// 回滚一个交易, reject 的交易都是未确认的交易
void Parser::rejectTx(class TxLog2 *txLog2) {
  // 同一个地址只能产生一条交易记录: address <-> tx <-> balance_diff
  map<int64_t, int64_t> addressBalance;

  _rejectTxInputs  (dbExplorer_, cache_, txLog2, addressBalance);
  _rejectTxOutputs (dbExplorer_, cache_, txLog2, addressBalance);
  _rejectAddressTxs(txLog2, addressBalance);
  _rejectTx        (dbExplorer_, cache_, txLog2);

  // 处理未确认计数器和记录
  removeUnconfirmedTxPool(txLog2);
}

// 获取上次 txlog2 的进度ID
int64_t Parser::getLastTxLog2Id() {
  MySQLResult res;
  int64_t lastID = 0;
  char **row = nullptr;
  string sql;

  // find last txlogs2 ID
  sql = "SELECT `value` FROM `0_explorer_meta` WHERE `key`='jiexi.last_txlog2_offset'";
  dbExplorer_.query(sql, res);
  if (res.numRows() == 1) {
    row = res.nextRow();
    lastID = strtoll(row[0], nullptr, 10);
    assert(lastID > 0);
  } else {
    const string now = date("%F %T");
    sql = Strings::Format("INSERT INTO `0_explorer_meta`(`key`,`value`,`created_at`,`updated_at`) "
                          " VALUES('jiexi.last_txlog2_offset', '0', '%s', '%s')",
                          now.c_str(), now.c_str());
    dbExplorer_.update(sql);
    lastID = 0;  // default value is zero
  }
  return lastID;
}

// 获取tx Hash & ID
static void _getTxInfo(MySQLConnection &db, class TxLog2 *txLog2) {
  MySQLResult res;
  string sql;
  char **row;
  const string txHashStr = txLog2->txHash_.ToString();

  //
  // get hex, id from DB
  //
  sql = Strings::Format("SELECT `hex`,`id` FROM `raw_txs_%04d` WHERE `tx_hash` = '%s' ",
                        HexToDecLast2Bytes(txHashStr) % 64, txHashStr.c_str());
  db.query(sql, res);
  if (res.numRows() == 0) {
    THROW_EXCEPTION_DBEX("can't find raw tx by hash: %s", txHashStr.c_str());
  }
  row = res.nextRow();
  txLog2->txHex_ = string(row[0]);
  txLog2->txId_  = atoi64(row[1]);

  //
  // Decode Tx Hex
  //
  if (!DecodeHexTx(txLog2->tx_, txLog2->txHex_)) {
    THROW_EXCEPTION_DBEX("TX decode failed, hex: %s", txLog2->txHex_.c_str());
  }
}

bool Parser::tryFetchTxLog2(class TxLog2 *txLog2, const int64_t lastId) {
  MySQLResult res;
  char **row = nullptr;
  string sql;

  if (isReverse_) {
    // 反向消费
//    sql = Strings::Format(" SELECT `id`, "
//                          "   `block_height`,`tx_hash`,`created_at` "
//                          " FROM `0_txlogs2` "
//                          " WHERE `id` < %lld ORDER BY `id` DESC LIMIT 1 ",
//                          lastTxLogOffset);
  } else {
    // batch_id 为 -1 表示最后的临时记录
    sql = Strings::Format(" SELECT `id`,`type`,`block_height`,`block_id`, "
                          " `max_block_timestamp`, `tx_hash`,`created_at` "
                          " FROM `0_txlogs2` "
                          " WHERE `id` > %lld AND `batch_id` <> -1 ORDER BY `id` ASC LIMIT 1 ",
                          lastId);
  }
  dbExplorer_.query(sql, res);
  if (res.numRows() == 0) {
    return false;
  }
  row = res.nextRow();

  txLog2->id_           = atoi64(row[0]);
  txLog2->type_         = atoi(row[1]);
  txLog2->blkHeight_    = atoi(row[2]);
  txLog2->blkId_        = atoi64(row[3]);
  txLog2->maxBlkTimestamp_ = (uint32_t)atoi64(row[4]);
  txLog2->ymd_          = atoi(date("%Y%m%d", txLog2->maxBlkTimestamp_).c_str());
  txLog2->txHash_       = uint256(row[5]);
  txLog2->createdAt_    = string(row[6]);

  if (!(txLog2->type_ == LOG2TYPE_TX_ACCEPT    ||
        txLog2->type_ == LOG2TYPE_TX_CONFIRM   ||
        txLog2->type_ == LOG2TYPE_TX_UNCONFIRM ||
        txLog2->type_ == LOG2TYPE_TX_REJECT)) {
    LOG_FATAL("invalid type: %d", txLog2->type_);
    return false;
  }

  // 反向消费，需要翻转类型
  if (isReverse_) {
    // TODO
//    if (txLog2->type_ == LOG2TYPE_TX_ACCEPT) {
//      txLog2->type_ = LOG2TYPE_TX_REJECT;
//    }
//    else if (txLog2->type_ == LOG2TYPE_TX_CONFIRM) {
//      txLog2->type_ = LOG2TYPE_TX_UNCONFIRM;
//    }
//    else if (txLog2->type_ == LOG2TYPE_TX_UNCONFIRM) {
//      txLog2->type_ = LOG2TYPE_TX_CONFIRM;
//    }
//    else if (txLog2->type_ == LOG2TYPE_TX_REJECT) {
//      txLog2->type_ = LOG2TYPE_TX_ACCEPT;
//    }
  }

  LOG_INFO("process txlog2(%c), logId: %d, type: %d, "
           "height: %d, tx hash: %s, created: %s",
           isReverse_ ? '-' : '+',
           txLog2->id_, txLog2->type_, txLog2->blkHeight_,
           txLog2->txHash_.ToString().c_str(), txLog2->createdAt_.c_str());

  // find raw tx hex & id
  _getTxInfo(dbExplorer_, txLog2);

  return true;
}

