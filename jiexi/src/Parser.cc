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
                                       int64_t txCount, int64_t unconfirmedTxCount,
                                       const char *address) {
  addrId_  = addrId;

  beginTxYmd_    = beginTxYmd;
  endTxYmd_      = endTxYmd;
  beginTxId_     = beginTxId;
  endTxId_       = endTxId;

  unconfirmedReceived_ = unconfirmedReceived;
  unconfirmedSent_     = unconfirmedSent;
  unconfirmedTxCount_  = unconfirmedTxCount;

  lastConfirmedTxYmd_ = lastConfirmedTxYmd;
  lastConfirmedTxId_  = lastConfirmedTxId;

  totalReceived_ = totalReceived;
  totalSent_     = totalSent;

  txCount_       = txCount;

  lastUseTime_ = time(nullptr);
  addressStr_  = string(address);
}

LastestAddressInfo::LastestAddressInfo(const LastestAddressInfo &a) {
  addrId_  = a.addrId_;

  beginTxYmd_    = a.beginTxYmd_;
  endTxYmd_      = a.endTxYmd_;
  beginTxId_     = a.beginTxId_;
  endTxId_       = a.endTxId_;

  unconfirmedReceived_ = a.unconfirmedReceived_;
  unconfirmedSent_     = a.unconfirmedSent_;
  unconfirmedTxCount_  = a.unconfirmedTxCount_;

  lastConfirmedTxYmd_ = a.lastConfirmedTxYmd_;
  lastConfirmedTxId_  = a.lastConfirmedTxId_;

  totalReceived_ = a.totalReceived_;
  totalSent_     = a.totalSent_;

  txCount_       = a.txCount_;

  lastUseTime_ = a.lastUseTime_;
  addressStr_  = a.addressStr_;
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



/////////////////////////////////  TxInfo  ///////////////////////////////////
TxInfo::TxInfo(const int64_t txId, const string &hex):
txId_(txId), hex_(hex), count_(0), useTime_(0)
{
}
TxInfo::TxInfo(const TxInfo &t) {
  txId_    = t.txId_;
  hex_     = t.hex_;
  count_   = t.count_;
  useTime_ = t.useTime_;
}
TxInfo::TxInfo():txId_(0), hex_(), count_(0), useTime_(0) {
}



/////////////////////////////////  TxInfoCache  ///////////////////////////////////
TxInfoCache::TxInfoCache() {
  lastClearTime_ = time(nullptr);
}

void TxInfoCache::getTxInfo(MySQLConnection &db,
                            const uint256 hash, int64_t *txId, string *hex) {
  if (cache_.find(hash) == cache_.end()) {
    string sql;
    MySQLResult res;
    char **row;
    const string txHashStr = hash.ToString();

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
    const string hex = string(row[0]);
    TxInfo txInfo(atoi64(row[1]), hex);
    cache_[hash] = txInfo;
  }

  const time_t now = time(nullptr);
  auto it = cache_.find(hash);
  assert(it != cache_.end());

  *txId = it->second.txId_;
  *hex  = it->second.hex_;
  it->second.useTime_ = now;
  it->second.count_++;

  // 一般而言，大部分 tx 会短时间内使用两次，一次是 accept, 一次是 confirm
  // 目标是让这部分 tx confirm 时少读取一次，提高 confirm 时的速度
  if (it->second.count_ >= 2) {
    cache_.erase(it);
  }

  // 触发回收空间，删除长时间没有使用的
  map<uint256, TxInfo> cacheNew;
  if (lastClearTime_ + 86400 < now) {
    lastClearTime_ = now;
    for (auto it2 = cache_.begin(); it2 != cache_.end(); it2++) {
      if (it->second.useTime_ + 86400 < now) {
        continue;
      }
      cacheNew[it2->first] = it2->second;
    }
    cache_.swap(cacheNew);
  }
}

/////////////////////////////////  Parser  ///////////////////////////////////
Parser::Parser():dbExplorer_(Config::GConfig.get("db.explorer.uri")),
running_(true), cache_(nullptr),
unconfirmedTxsSize_(0), unconfirmedTxsCount_(0), blkTs_(2016)
{
  // setup cache manager: SSDB
  cacheEnable_ = Config::GConfig.getBool("ssdb.enable", false);
  if (cacheEnable_) {
    cache_ = new CacheManager(Config::GConfig.get("ssdb.host"),
                              (int32_t)Config::GConfig.getInt("ssdb.port"),
                              Config::GConfig.get("url.file.dir", ""));
  }
  notifyFileLog2Producer_ = Config::GConfig.get("notify.log2producer.file");
  if (notifyFileLog2Producer_.empty()) {
    THROW_EXCEPTION_DBEX("empty config: notify.log2producer.file");
  }
}

Parser::~Parser() {
  stop();

  if (cache_ != nullptr) {
    delete cache_;
    cache_ = nullptr;
  }

  if (watchNotifyThread_.joinable()) {
    watchNotifyThread_.join();
  }
}

void Parser::stop() {
  if (running_) {
    running_ = false;
    LOG_INFO("stop tparser");
  }

  inotify_.RemoveAll();
  changed_.notify_all();
}

bool Parser::init() {
  string sql;
  MySQLResult res;
  char **row = nullptr;
  const string nowStr = date("%F %T");

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
    unconfirmedTxsSize_  = atoi64(row[1]);

    // 严禁使用MYSQL函数NOW()，防止时区错乱
    sql = Strings::Format("INSERT INTO `0_explorer_meta` (`key`, `value`, `created_at`, `updated_at`)"
                          " VALUES ('jiexi.unconfirmed_txs.count',  '%d', '%s', '%s'), "
                          "        ('jiexi.unconfirmed_txs.size', '%lld', '%s', '%s') ",
                          unconfirmedTxsCount_, nowStr.c_str(), nowStr.c_str(),
                          unconfirmedTxsSize_, nowStr.c_str(), nowStr.c_str());
    dbExplorer_.updateOrThrowEx(sql, 2);
  }

  //
  // 获取最近 2016 个块的时间戳
  //
  {
    sql = Strings::Format("SELECT * FROM (SELECT `timestamp`,`height` FROM `0_blocks`"
                          "  WHERE `chain_id` = 0 ORDER BY `height` DESC LIMIT 2016) "
                          " AS `t1` ORDER BY `height` ASC ");
    dbExplorer_.query(sql, res);
    if (res.numRows() == 0) {
      THROW_EXCEPTION_DBEX("can't find max block timestamp");
    }
    while ((row = res.nextRow()) != nullptr) {
      blkTs_.pushBlock(atoi(row[1]), atoi64(row[0]));
    }
    LOG_INFO("found max block timestamp: %lld", blkTs_.getMaxTimestamp());
  }

  watchNotifyThread_ = thread(&Parser::threadWatchNotifyFile, this);

  return true;
}

void Parser::threadWatchNotifyFile() {
  try {
    //
    // IN_CLOSE_NOWRITE :
    //     一个以只读方式打开的文件或目录被关闭
    //     A file or directory that had been open read-only was closed.
    // `cat FILE` 可触发该事件
    //
    InotifyWatch watch(notifyFileLog2Producer_, IN_CLOSE_NOWRITE);
    inotify_.Add(watch);
    LOG_INFO("watching notify file: %s", notifyFileLog2Producer_.c_str());

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

void Parser::run() {
  TxLog2 txLog2, lastTxLog2;
  string blockHash;  // 目前仅用在URL回调上
  int64_t lastTxLog2Id = 0;

  // 检测 address_txs_<YYYYMM> 是否存在:  UNCONFIRM_TX_TIMESTAMP(2030-01-01)
  checkTableAddressTxs(UNCONFIRM_TX_TIMESTAMP);

  while (running_) {
    try {
      lastTxLog2Id = getLastTxLog2Id();

      if (tryFetchTxLog2(&txLog2, lastTxLog2Id) == false) {
        UniqueLock ul(lock_);
        // 默认等待N毫秒，直至超时，中间有人触发，则立即continue读取记录
        changed_.wait_for(ul, chrono::milliseconds(10*1000));
        continue;
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
        //
        // acceptTx() 时，当前向交易不存在，我们则删除掉当前 txlog2，并抛出异常。
        //
        acceptTx(&txLog2);
      }
      else if (txLog2.type_ == LOG2TYPE_TX_CONFIRM) {
        // confirm 时才能 acceptBlock()
        if (txLog2.tx_.IsCoinBase()) {
          acceptBlock(&txLog2, blockHash);
        }
        //
        // 有可能因为前向交易不存在，导致该tx没有被accept，但log2producer认为accept过了
        // 所以，这里检测一下，若没有accept，则执行accept操作
        //
        const int32_t blockHeight = txLog2.blkHeight_;
        const int64_t blockId     = txLog2.blkId_;
        const int32_t ymd         = txLog2.ymd_;
        const int32_t maxBlkTs    = txLog2.maxBlkTimestamp_;
        if (!hasAccepted(&txLog2)) {
          txLog2.type_ = LOG2TYPE_TX_ACCEPT;
          txLog2.blkHeight_ = -1;
          txLog2.blkId_     = -1;
          txLog2.ymd_       = UNCONFIRM_TX_YMD;
          txLog2.maxBlkTimestamp_ = UNCONFIRM_TX_TIMESTAMP;

          acceptTx(&txLog2);

          txLog2.type_ = LOG2TYPE_TX_CONFIRM;
          txLog2.blkHeight_ = blockHeight;
          txLog2.blkId_     = blockId;
          txLog2.ymd_       = ymd;
          txLog2.maxBlkTimestamp_ = maxBlkTs;
        }
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
    } /* /try */
    catch (int e) {
      // 整数异常，都是可以继续的异常
      LOG_FATAL("tparser:run exception: %d", e);
      continue;
    }
  } /* /while */

  return;

error:
  dbExplorer_.execute("ROLLBACK");
  return;
}

bool Parser::hasAccepted(class TxLog2 *txLog2) {
  MySQLResult res;
  string sql;
  char **row = nullptr;
  const string hash = txLog2->txHash_.ToString();

  // 存在于 table.txs_xxxx 表中，说明已经 accept 处理过了
  sql = Strings::Format("SELECT `height` FROM `txs_%04d` WHERE `hash` = '%s'",
                        HexToDecLast2Bytes(hash) % 64, hash.c_str());
  dbExplorer_.query(sql, res);
  if (res.numRows() == 1) {
    row = res.nextRow();
    assert(atoi(row[0]) == 0);  // 确认数应该为零
    return true;
  }
  return false;
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
void Parser::txsHash2ids(const std::set<uint256> &hashVec,
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
      THROW_EXCEPTION_DBEX("can't find tx's ID, hash: %s", hashStr.c_str());
    }
    row = res.nextRow();
    hash2id.insert(std::make_pair(hash, atoi64(row[0])));
  }

  assert(hash2id.size() >= hashVec.size());
}

void Parser::updateLastTxlog2Id(const int64_t newId) {
  string sql = Strings::Format("UPDATE `0_explorer_meta` SET `value` = '%lld',`updated_at`='%s' "
                               " WHERE `key`='jiexi.last_txlog2_offset'",
                               newId, date("%F %T").c_str());
  dbExplorer_.updateOrThrowEx(sql, 1);
}

void _insertBlock(MySQLConnection &db, const CBlock &blk,
                  const int64_t blockId, const int32_t height,
                  const int32_t blockBytes, const int64_t maxBlockTimestamp) {
  CBlockHeader header = blk.GetBlockHeader();  // alias
  uint256 blockHash = blk.GetHash();
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
    const uint64_t pdiff = TargetToPdiff(blockHash);

    const int64_t rewardBlock = GetBlockValue(height, 0);
    const int64_t rewardFees  = blk.vtx[0].GetValueOut() - rewardBlock;
    assert(rewardFees >= 0);
    string sql1 = Strings::Format("INSERT INTO `0_blocks` (`block_id`, `height`, `hash`,"
                                  " `version`, `mrkl_root`, `timestamp`,`curr_max_timestamp`, `bits`, `nonce`,"
                                  " `prev_block_id`, `prev_block_hash`, `next_block_id`, "
                                  " `next_block_hash`, `chain_id`, `size`,"
                                  " `pool_difficulty`,`difficulty`, `difficulty_double`, "
                                  " `tx_count`, `reward_block`, `reward_fees`, "
                                  " `created_at`) VALUES ("
                                  // 1. `block_id`, `height`, `hash`, `version`, `mrkl_root`, `timestamp`,
                                  " %lld, %d, '%s', %d, '%s', %u, "
                                  // 2. `curr_max_timestamp`, `bits`, `nonce`, `prev_block_id`, `prev_block_hash`,
                                  " %lld, %u, %u, %lld, '%s', "
                                  // 3. `next_block_id`, `next_block_hash`, `chain_id`, `size`,
                                  " 0, '', %d, %d, "
                                  // 4. `pool_difficulty`,`difficulty`, `difficulty_double`, `tx_count`,
                                  " %llu, %llu, %f, %d, "
                                  // 5. `reward_block`, `reward_fees`, `created_at`
                                  " %lld, %lld, '%s');",
                                  // 1.
                                  blockId, height,
                                  blockHash.ToString().c_str(),
                                  header.nVersion,
                                  header.hashMerkleRoot.ToString().c_str(),
                                  (uint32_t)header.nTime, maxBlockTimestamp,
                                  // 2.
                                  header.nBits, header.nNonce, prevBlockId, prevBlockHash.c_str(),
                                  // 3.
                                  0/* chainId */, blockBytes,
                                  // 4.
                                  pdiff, difficulty, difficulty_double, blk.vtx.size(),
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

  // 先加入到块时间戳里，重新计算时间戳. 回滚块的时候是最后再 pop 块
  blkTs_.pushBlock(txLog2->blkHeight_, blk.GetBlockTime());

  // 插入数据至 table.0_blocks
  _insertBlock(dbExplorer_, blk, txLog2->blkId_,
               txLog2->blkHeight_, (int32_t)blkRawHex.length()/2, blkTs_.getMaxTimestamp());

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

void Parser::_accpetTx_insertTxInputs(TxLog2 *txLog2,
                                      map<int64_t, int64_t> &addressBalance,
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
      prevTxId = txHash2Id(dbExplorer_, prevHash);
      prevPos  = (int32_t)in.prevout.n;

      // 将前向交易标记为已花费
      sql = Strings::Format("UPDATE `tx_outputs_%04d` SET "
                            " `spent_tx_id`=%lld, `spent_position`=%d"
                            " WHERE `tx_id`=%lld AND `position`=%d "
                            " AND `spent_tx_id`=0 AND `spent_position`=-1 ",
                            prevTxId % 100, txLog2->txId_, n,
                            prevTxId, prevPos);
      if (dbExplorer_.update(sql) != 1) {
        assert(txLog2->type_ == LOG2TYPE_TX_ACCEPT);
        //
        // 说明前向交易不存在，忽略该 txlog2
        // accept tx 前向 tx 不存在时，跳过某个 txlog2. 因为 log1producer 初始化
        // 同步时，追 bitcoind 会忽略中间的tx（tx很久都没有得到确认），而后某个tx依赖此，
        // 则导致无法accept后一个tx.
        //
        // 意味这条 txlog2 会引发系统异常，我们删除掉这条 txlog2
        //
        dbExplorer_.execute("ROLLBACK");  // 回滚掉当前事务
        // 执行删除
        LOG_WARN("delete txlog2: %s", txLog2->toString().c_str());
        string delSql = Strings::Format("DELETE FROM `0_txlogs2` WHERE `id`=%lld", txLog2->id_);
        dbExplorer_.updateOrThrowEx(delSql, 1);

        // 抛出异常
        LOG_FATAL("invalid input: %lld:%s:%d, prev output: %lld:%s:%d",
                  txLog2->txId_, txLog2->txHash_.ToString().c_str(), n,
                  prevTxId, prevHash.ToString().c_str(), prevPos);
        throw EXCEPTION_TPARSER_TX_INVALID_INPUT;
      }

      // 将 address_unspent_outputs_xxxx 相关记录删除
      _removeUnspentOutputs(dbExplorer_, prevTxId, prevPos, addressBalance);

      // 插入当前交易的inputs
      DBTxOutput dbTxOutput = getTxOutput(dbExplorer_, prevTxId, prevPos);
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
    }
  } /* /for */

  // 执行插入 inputs
  if (!multiInsert(dbExplorer_, tableName, fields, values)) {
    THROW_EXCEPTION_DBEX("insert inputs fail, txId: %lld, hash: %s",
                         txLog2->txId_, txLog2->tx_.GetHash().ToString().c_str());
  }
}


static
void _accpetTx_insertTxOutputs(MySQLConnection &db,
                               TxLog2 *txLog2, map<int64_t, int64_t> &addressBalance) {
  int n;
  const string now = date("%F %T");
  set<string> allAddresss;

  // 提取涉及到的所有地址
  n = -1;
  for (auto &out : txLog2->tx_.vout) {
    n++;
    txnouttype type;
    vector<CTxDestination> addresses;
    int nRequired;
    if (!ExtractDestinations(out.scriptPubKey, type, addresses, nRequired)) {
      LOG_WARN("extract destinations failure, txId: %lld, hash: %s, position: %d",
               txLog2->txId_, txLog2->tx_.GetHash().ToString().c_str(), n);
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
    ExtractDestinations(out.scriptPubKey, type, addresses, nRequired);

    // 解地址可能失败，但依然有 tx_outputs_xxxx 记录
    // 输出无有效地址的奇葩TX:
    //   testnet3: e920604f540fec21f66b6a94d59ca8b1fbde27fc7b4bc8163b3ede1a1f90c245

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
                          " `last_confirmed_tx_id`, `last_confirmed_tx_ymd`, `address`, `unconfirmed_tx_count` "
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
                                      atoi64(row[6]),   // totalCount
                                      atoi64(row[12]),  // unconfirmedTxCount
                                      row[11]           // address
                                      );

    gAddrTxCache[addrID] = ptr;
    it = gAddrTxCache.find(addrID);

    // clear, 数量限制，防止长时间运行后占用过多内存
    const size_t  kMaxCacheCount  = IsDebug() ? 20*10000 : 1000*10000;
    const int32_t kExpiredSeconds = IsDebug() ? 3600*5 : 86400 * 3;
    if (gAddrTxCache.size() > kMaxCacheCount) {
      LOG_INFO("clear gAddrTxCache begin, count: %llu", gAddrTxCache.size());
      for (auto it2 = gAddrTxCache.begin(); it2 != gAddrTxCache.end(); ) {
        if (it2->second->lastUseTime_ + kExpiredSeconds > now) {
          it2++;
          continue;
        }
        // 删除超出超过 kExpiredDays 天的记录
        delete it2->second;
        gAddrTxCache.erase(it2++);
      }
      LOG_INFO("clear gAddrTxCache end, count: %llu", gAddrTxCache.size());
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
    // update 2015-10-25: 不对未确认交易做统计余额变化统计：total_received, balance_final
    //                    在未确认交易里，置为-1. 不做这两个字段统计才可以快速交易任意两个未确认节点
    //                    而不是要依次移动才能完成交换。
    //
    sql = Strings::Format("INSERT INTO `address_txs_%d` (`address_id`, `tx_id`, `tx_height`,"
                          " `total_received`, `balance_diff`, `balance_final`, `idx`, `ymd`,"
                          " `prev_ymd`, `prev_tx_id`, `next_ymd`, `next_tx_id`, `created_at`)"
                          " VALUES (%lld, %lld, %d, -1, %lld, -1, %lld, %d, "
                          "         %d, %lld, 0, 0, '%s') ",
                          tableIdx_AddrTxs(txLog2->ymd_), addrID, txLog2->txId_, txLog2->blkHeight_,
                          balanceDiff, addr->txCount_+ 1, txLog2->ymd_,
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
                          " `unconfirmed_tx_count` = `unconfirmed_tx_count` + 1, "
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
    addr->unconfirmedTxCount_++;
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
    fee = valueOut;
  } else {
    fee = valueIn - valueOut;
  }

  sql = Strings::Format("INSERT INTO `%s`(`tx_id`, `hash`, `height`, `ymd`, `is_coinbase`,"
                        " `version`, `lock_time`, `size`, `fee`, `total_in_value`, "
                        " `total_out_value`, `inputs_count`, `outputs_count`, `created_at`)"
                        " VALUES (%lld, '%s', %d, %d, %d, %d, %u, %d, %lld, %lld, %lld, %d, %d, '%s') ",
                        tName.c_str(),
                        // `tx_id`, `hash`, `height`
                        txLog2->txId_, txLog2->txHash_.ToString().c_str(),
                        txLog2->blkHeight_, txLog2->ymd_,
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
  sql = Strings::Format("UPDATE `0_explorer_meta` SET `value`='%d', `updated_at`='%s' "
                        " WHERE `key` = 'jiexi.unconfirmed_txs.count' ",
                        unconfirmedTxsCount_, nowStr.c_str());
  dbExplorer_.updateOrThrowEx(sql, 1);

  //
  // jiexi.unconfirmed_txs.size
  //
  unconfirmedTxsSize_ += txLog2->txHex_.length() / 2;
  sql = Strings::Format("UPDATE `0_explorer_meta` SET `value`='%lld', `updated_at`='%s' "
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
  sql = Strings::Format("UPDATE `0_explorer_meta` SET `value`='%d', `updated_at`='%s' "
                        " WHERE `key` = 'jiexi.unconfirmed_txs.count' ",
                        unconfirmedTxsCount_, nowStr.c_str());
  dbExplorer_.updateOrThrowEx(sql, 1);

  //
  // jiexi.unconfirmed_txs.size
  //
  unconfirmedTxsSize_ -= txLog2->txHex_.length() / 2;
  assert(unconfirmedTxsSize_ >= 0);
  sql = Strings::Format("UPDATE `0_explorer_meta` SET `value`='%lld', `updated_at`='%s' "
                        " WHERE `key` = 'jiexi.unconfirmed_txs.size' ",
                        unconfirmedTxsSize_, nowStr.c_str());
  dbExplorer_.updateOrThrowEx(sql, 1);
}

//
// 更新节点的YMD
//
void Parser::_updateTxNodeYmd(LastestAddressInfo *addr, AddressTxNode *node,
                              const int32_t targetYmd) {
  string sql;

  //
  // 变更日期导致记录跨表，需要移动数据至新表中
  // 跨表了，需要移动数据. 因未确认交易都在特定日期中，这里必然跨表
  //
  assert(tableIdx_AddrTxs(targetYmd) != tableIdx_AddrTxs(node->ymd_));

  //
  // 变更前向节点的后续指向
  //
  if (node->prevTxId_ > 0) {
    sql = Strings::Format("UPDATE `address_txs_%d` SET `next_ymd`=%d "
                          " WHERE `address_id`=%lld AND `tx_id`=%lld ",
                          tableIdx_AddrTxs(node->prevYmd_),
                          targetYmd, node->addressId_, node->prevTxId_);
    dbExplorer_.updateOrThrowEx(sql , 1);
  } else {
    // 变更地址的头交易信息，没有前向节点则说明当前节点就是头节点
    sql = Strings::Format("UPDATE `addresses_%04d` SET "
                          " `begin_tx_ymd`=%d WHERE `id`=%lld ",
                          tableIdx_Addr(addr->addrId_), targetYmd, addr->addrId_);
    dbExplorer_.updateOrThrowEx(sql , 1);
    addr->beginTxYmd_ = targetYmd;
  }

  //
  // 变更后向节点的前向指向
  //
  if (node->nextTxId_ > 0) {
    // 涉及指向，不涉及金额变化
    sql = Strings::Format("UPDATE `address_txs_%d` SET `prev_ymd`=%d "
                          " WHERE `address_id`=%lld AND `tx_id`=%lld ",
                          tableIdx_AddrTxs(node->nextYmd_),
                          targetYmd, node->addressId_, node->nextTxId_);
    dbExplorer_.updateOrThrowEx(sql , 1);
  } else {
    // 变更地址的尾交易信息，没有后向节点则说明当前节点就是尾节点
    sql = Strings::Format("UPDATE `addresses_%04d` SET "
                          " `end_tx_ymd`=%d WHERE `id`=%lld ",
                          tableIdx_Addr(addr->addrId_), targetYmd, addr->addrId_);
    dbExplorer_.updateOrThrowEx(sql , 1);
    addr->endTxYmd_ = targetYmd;
  }

  //
  // 插入记录至新表
  //
  // 表列名
  const string fields1 = "`address_id`, `tx_id`, `tx_height`, `total_received`, "
  "`balance_diff`, `balance_final`, `idx`, `ymd`, `prev_ymd`, "
  "`prev_tx_id`, `next_ymd`, `next_tx_id`, `created_at`";
  // 表列名 (更新了 ymd)
  const string fields2 = Strings::Format("`address_id`, `tx_id`, `tx_height`, `total_received`, "
                                         "`balance_diff`, `balance_final`, `idx`, %d, `prev_ymd`, "
                                         "`prev_tx_id`, `next_ymd`, `next_tx_id`, `created_at`",
                                         targetYmd);
  sql = Strings::Format("INSERT INTO `address_txs_%d` (%s) "
                        " SELECT %s FROM `address_txs_%d` "
                        " WHERE `address_id`=%lld AND `tx_id`=%lld ",
                        tableIdx_AddrTxs(targetYmd),  fields1.c_str(),
                        fields2.c_str(), tableIdx_AddrTxs(node->ymd_),
                        node->addressId_, node->txId_);
  dbExplorer_.updateOrThrowEx(sql, 1);

  // 删除旧记录
  sql = Strings::Format("DELETE FROM `address_txs_%d` "
                        " WHERE `address_id`=%lld AND `tx_id`=%lld ",
                        tableIdx_AddrTxs(node->ymd_), node->addressId_, node->txId_);
  dbExplorer_.updateOrThrowEx(sql, 1);
}

// 交换两个未确认的交易节点, 可跨过N个节点直接交换
// m必须在n前面
void Parser::_switchUnconfirmedAddressTxNode(LastestAddressInfo *addr,
                                             AddressTxNode *m, AddressTxNode *n) {
  //
  // 示意：..., m, ..., n, ...
  // 要求：两个节点均是未确认的，那么m以后的所有节点均应为未确认的
  //
  assert(m->ymd_ == UNCONFIRM_TX_YMD);
  assert(n->ymd_ == UNCONFIRM_TX_YMD);
  assert(addr->endTxYmd_ == UNCONFIRM_TX_YMD);
  assert(m->idx_ < n->idx_);

  const int64_t addrId = addr->addrId_;
  string sql;
  string sql_m_update, sql_n_update;

  bool isNeighbour = false;  // m, n是否相邻
  if (n->prevTxId_ == m->txId_) {
    isNeighbour = true;
    assert(m->nextTxId_ == n->txId_);
  }

  //
  // 处理N的后向节点
  //
  if (n->nextTxId_ > 0) {
    sql = Strings::Format("UPDATE `address_txs_%d` SET `prev_tx_id`=%lld "
                          " WHERE `address_id`=%lld AND `tx_id`=%lld ",
                          tableIdx_AddrTxs(UNCONFIRM_TX_YMD),
                          m->txId_, addrId, n->nextTxId_);
    dbExplorer_.updateOrThrowEx(sql , 1);
  } else {
    // N没有后续节点，变更地址的尾交易信息
    sql = Strings::Format("UPDATE `addresses_%04d` SET "
                          " `end_tx_id`=%lld WHERE `id`=%lld ",
                          tableIdx_Addr(addrId), m->txId_, addrId);
    dbExplorer_.updateOrThrowEx(sql , 1);

    addr->endTxId_ = m->txId_;
  }
  sql_m_update += Strings::Format("`next_ymd`=%d,`next_tx_id`=%lld,",
                                  n->nextYmd_, n->nextTxId_);

  //
  // 处理N的前向节点, 必然存在该节点
  //
  if (!isNeighbour) {
    sql = Strings::Format("UPDATE `address_txs_%d` SET `next_tx_id`=%lld "
                          " WHERE `address_id`=%lld AND `tx_id`=%lld ",
                          tableIdx_AddrTxs(UNCONFIRM_TX_YMD),
                          m->txId_, addrId, n->prevTxId_);
    dbExplorer_.updateOrThrowEx(sql , 1);

    sql_m_update += Strings::Format("`prev_ymd`=%d,`prev_tx_id`=%lld,",
                                    n->prevYmd_, n->prevTxId_);
  } else {
    sql_m_update += Strings::Format("`prev_ymd`=%d,`prev_tx_id`=%lld,",
                                    n->ymd_, n->txId_);
  }


  //
  // 处理M的前向节点，该节点可能是确认的、也可能是未确认的，该节点的后向节点必然未确认
  //
  if (m->prevTxId_ > 0) {
    sql = Strings::Format("UPDATE `address_txs_%d` SET `next_tx_id`=%lld "
                          " WHERE `address_id`=%lld AND `tx_id`=%lld ",
                          tableIdx_AddrTxs(m->prevYmd_), n->txId_,
                          addrId, m->prevTxId_);
    dbExplorer_.updateOrThrowEx(sql , 1);
  } else {
    // M的前向节点不存在，变更地址的头交易信息
    sql = Strings::Format("UPDATE `addresses_%04d` SET "
                          " `begin_tx_id`=%lld WHERE `id`=%lld ",
                          tableIdx_Addr(addrId), n->txId_, addrId);
    dbExplorer_.updateOrThrowEx(sql , 1);

    addr->beginTxId_ = n->txId_;
    assert(addr->beginTxYmd_ == UNCONFIRM_TX_YMD);
  }
  sql_n_update += Strings::Format("`prev_ymd`=%d,`prev_tx_id`=%lld,",
                                  m->prevYmd_, m->prevTxId_);

  //
  // 处理M的后向节点, 必然存在该节点
  //
  if (!isNeighbour) {
    sql = Strings::Format("UPDATE `address_txs_%d` SET `prev_tx_id`=%lld "
                          " WHERE `address_id`=%lld AND `tx_id`=%lld ",
                          tableIdx_AddrTxs(UNCONFIRM_TX_YMD),
                          n->txId_, addrId, m->nextTxId_);
    dbExplorer_.updateOrThrowEx(sql , 1);

    sql_n_update += Strings::Format("`next_ymd`=%d,`next_tx_id`=%lld,",
                                    m->nextYmd_, m->nextTxId_);
  } else {
    sql_n_update += Strings::Format("`next_ymd`=%d,`next_tx_id`=%lld,",
                                    m->ymd_, m->txId_);
  }


  //
  // 交换M和N的节点部分信息
  //
  {
    // address_id + idx 为唯一索引，所以需要先置为负的，再设置
    sql = Strings::Format("UPDATE `address_txs_%d` SET `idx`=%lld "
                          " WHERE `address_id`=%lld AND `tx_id`=%lld ",
                          tableIdx_AddrTxs(UNCONFIRM_TX_YMD),
                          -1 * m->idx_, addrId, m->txId_);
    dbExplorer_.updateOrThrowEx(sql , 1);

    sql_n_update.resize(sql_n_update.size() - 1);
    sql = Strings::Format("UPDATE `address_txs_%d` SET `idx`=%lld, %s "
                          " WHERE `address_id`=%lld AND `tx_id`=%lld ",
                          tableIdx_AddrTxs(UNCONFIRM_TX_YMD),
                          m->idx_, sql_n_update.c_str(), addrId, n->txId_);
    dbExplorer_.updateOrThrowEx(sql , 1);

    sql_m_update.resize(sql_m_update.size() - 1);
    sql = Strings::Format("UPDATE `address_txs_%d` SET `idx`=%lld, %s "
                          " WHERE `address_id`=%lld AND `tx_id`=%lld ",
                          tableIdx_AddrTxs(UNCONFIRM_TX_YMD),
                          n->idx_, sql_m_update.c_str(), addrId, m->txId_);
    dbExplorer_.updateOrThrowEx(sql , 1);
  }
}

// 移动未确认的交易节点至第一个未确认的节点
void Parser::_confirmTx_MoveToFirstUnconfirmed(LastestAddressInfo *addr,
                                               AddressTxNode *node) {
  assert(addr->lastConfirmedTxId_ != node->prevTxId_);

  AddressTxNode firstUnconfirmedNode;
  if (addr->lastConfirmedTxId_ > 0) {
    // 有确认节点，提取确认节点后的首个未确认节点
    AddressTxNode lastConfirmedNode;
    _getAddressTxNode(addr->lastConfirmedTxId_, addr, &lastConfirmedNode);
    _getAddressTxNode(lastConfirmedNode.nextTxId_, addr, &firstUnconfirmedNode);
  } else {
    // 无确认节点，第一个节点就是未确认节点
    _getAddressTxNode(addr->beginTxId_, addr, &firstUnconfirmedNode);
  }

  LOG_INFO("[confirmTx_MoveToFirstUnconfirmed] address_id: %lld, txid: %lld <-> %lld",
           addr->addrId_, firstUnconfirmedNode.txId_, node->txId_);
  _switchUnconfirmedAddressTxNode(addr, &firstUnconfirmedNode, node);
}

// 将当前未确认节点移动至交易链的末尾
void Parser::_rejectTx_MoveToLastUnconfirmed(LastestAddressInfo *addr, AddressTxNode *node) {
  assert(addr->endTxId_ != node->txId_);
  assert(addr->endTxYmd_ == UNCONFIRM_TX_YMD);

  AddressTxNode lastNode;
  _getAddressTxNode(addr->endTxId_, addr, &lastNode);

  LOG_INFO("[rejectTx_MoveToLastUnconfirmed] address_id: %lld, txid: %lld <-> %lld",
           addr->addrId_, node->txId_, lastNode.txId_);
  _switchUnconfirmedAddressTxNode(addr, node, &lastNode);
}

void Parser::removeAddressCache(const map<int64_t, int64_t> &addressBalance,
                                const int32_t ymd) {
  if (!cacheEnable_) {
    return;
  }

  //
  // http://twiki.bitmain.com/bin/view/Main/SSDB-Cache
  //
  for (auto it : addressBalance) {
    const int64_t addrID = it.first;
    LastestAddressInfo *addr = _getAddressInfo(dbExplorer_, addrID);

    // 清理 addr_xxxx 的缓存计数器
    cache_->insertKV(Strings::Format("addr_%s", addr->addressStr_.c_str()));

    // 清理 address_txs_xxxx 的缓存计数器
    cache_->insertHashSet(addr->addressStr_,
                          Strings::Format("address_txs_%d", tableIdx_AddrTxs(ymd)));
  }
}

void Parser::removeTxCache(const uint256 &txHash) {
  if (!cacheEnable_) {
    return;
  }

  //
  // http://twiki.bitmain.com/bin/view/Main/SSDB-Cache
  //
  // 删除: tx_{hash}
  cache_->insertKV(Strings::Format("tx_%s", txHash.ToString().c_str()));
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
  _accpetTx_insertTxInputs(txLog2, addressBalance, valueIn);

  // 处理outputs
  _accpetTx_insertTxOutputs(dbExplorer_, txLog2, addressBalance);

  // 缓存 addressBalance, 减少 confirm tx 操作时查找关联地址变更记录的开销
  _setTxAddressBalance(txLog2, addressBalance);

  // 变更地址相关信息
  _accpetTx_insertAddressTxs(dbExplorer_, txLog2, addressBalance);

  // 插入交易tx
  _accpetTx_insertTx(dbExplorer_, txLog2, valueIn);

  // 处理未确认计数器和记录
  addUnconfirmedTxPool(txLog2);

  // cache
  removeAddressCache(addressBalance, txLog2->ymd_);
  if (!txLog2->tx_.IsCoinBase()) {
    for (auto &in : txLog2->tx_.vin) {
      removeTxCache(in.prevout.hash);
    }
  }
}

// 设置地址的余额变更情况
void Parser::_setTxAddressBalance(class TxLog2 *txLog2,
                                  const map<int64_t, int64_t> &addressBalance) {
  assert(addressBalanceCache_.find(txLog2->txHash_) == addressBalanceCache_.end());
  addressBalanceCache_[txLog2->txHash_] = addressBalance;

  // 读取一次：更新tx的最后读取时间
  _getTxAddressBalance(txLog2->txId_, txLog2->txHash_, txLog2->tx_);
}

// 获取tx对应各个地址的余额变更情况
map<int64_t, int64_t> *Parser::_getTxAddressBalance(const int64_t txID,
                                                    const uint256 &txHash,
                                                    const CTransaction &tx) {
  // 存放每个tx的读取时间
  static map<uint256, time_t> txsTime_;

  txsTime_[txHash] = time(nullptr);

  if (addressBalanceCache_.find(txHash) != addressBalanceCache_.end()) {
    return &(addressBalanceCache_[txHash]);
  }

  map<int64_t/* addrID */, int64_t/*  balance diff */> addressBalance;

  MySQLResult res;
  string sql;
  char **row;
  set<string> allAddresss;
  int n;

  //
  // vin
  //
  if (!tx.IsCoinBase()) {
    for (auto &in : tx.vin) {
      uint256 prevHash = in.prevout.hash;
      int64_t prevTxId = txHash2Id(dbExplorer_, prevHash);
      int32_t prevPos  = (int32_t)in.prevout.n;

      // 获取相关output信息
      sql = Strings::Format("SELECT `address_ids`,`value` FROM `tx_outputs_%04d` "
                            " WHERE `tx_id`=%lld AND `position`=%d",
                            tableIdx_TxOutput(prevTxId), prevTxId, prevPos);
      dbExplorer_.query(sql, res);
      assert(res.numRows() == 1);

      // 获取地址
      row = res.nextRow();
      vector<string> addressIdsStrVec = split(string(row[0]), '|');
      for (auto &addrIdStr : addressIdsStrVec) {
        addressBalance[atoi64(addrIdStr.c_str())] += -1 * atoi64(row[1])/* value */;
      }
    }
  }

  //
  // vout
  //
  // 提取涉及到的所有地址
  n = -1;
  for (auto &out : tx.vout) {
    n++;
    txnouttype type;
    vector<CTxDestination> addresses;
    int nRequired;
    if (!ExtractDestinations(out.scriptPubKey, type, addresses, nRequired)) {
      LOG_WARN("extract destinations failure, txId: %lld, hash: %s, position: %d",
               txID, txHash.ToString().c_str(), n);
      continue;
    }
    for (auto &addr : addresses) {  // multiSig 可能由多个输出地址
      const string addrStr = CBitcoinAddress(addr).ToString();
      allAddresss.insert(CBitcoinAddress(addr).ToString());
    }
  }
  // 拿到所有地址的id
  map<string, int64_t> addrMap;
  GetAddressIds(dbExplorer_, allAddresss, addrMap);

  for (auto &out : tx.vout) {
    txnouttype type;
    vector<CTxDestination> addresses;
    int nRequired;
    ExtractDestinations(out.scriptPubKey, type, addresses, nRequired);

    // multiSig 可能由多个输出地址: https://en.bitcoin.it/wiki/BIP_0011
    for (auto &addr : addresses) {
      const string addrStr = CBitcoinAddress(addr).ToString();
      const int64_t addrId = addrMap[addrStr];
      // 增加每个地址的余额
      addressBalance[addrId] += out.nValue;
    }
  }

  // 设置缓存
  addressBalanceCache_[txHash] = addressBalance;

  //
  // 删掉过期的数据
  //
  // 超过 50万 记录数则触发, 每天大约TX数量, 1000 * 144 = 14万
  const int kMaxItemsCount     = 100 * 10000;
  const time_t kExpiredSeconds = 86400 * 3;
  if (addressBalanceCache_.size() > kMaxItemsCount) {
    LOG_INFO("clear addressBalanceCache start, count: %llu", addressBalanceCache_.size());

    // 删掉最近未使用的tx
    for (auto it = txsTime_.begin(); it != txsTime_.end(); ) {
      if (it->second + kExpiredSeconds < time(nullptr)) {
        txsTime_.erase(it++);
      } else {
        it++;
      }
    }
    // 已 txsTime_ 涉及的 tx 为准，不存在的都删除
    for (auto it = addressBalanceCache_.begin(); it != addressBalanceCache_.end(); ) {
      if (txsTime_.count(it->first) == 0) {
        addressBalanceCache_.erase(it++);
      } else {
        it++;
      }
    }
    LOG_INFO("clear addressBalanceCache end, count: %llu", addressBalanceCache_.size());
    assert(addressBalanceCache_.size() == txsTime_.size());
  }

  assert(addressBalanceCache_.count(txHash) != 0);
  return &(addressBalanceCache_[txHash]);
}

int32_t prevYmd(const int32_t ymd) {
  if (ymd == UNCONFIRM_TX_YMD) {
    return atoi(date("%Y%m%d").c_str());  // 若为 UNCONFIRM_TX_YMD, 则为当前天
  }

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
          break;  // 抵达最早的日期了
        }
        if (tableIdx_AddrTxs(ymd) != currTableIdx) {
          break;  // 找到前一个表了（目前是月表）
        }
      }

      continue;
    }

    assert(res.numRows() == 1);
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
  // 获取前向节点，用于设置部分字段。已确认节点才会设置: `total_received`, `balance_final`
  //
  string sql2;
  if (addr->lastConfirmedTxId_ != 0) {
    assert(node->prevTxId_ == addr->lastConfirmedTxId_);

    AddressTxNode lastConfirmedNode;
    _getAddressTxNode(addr->lastConfirmedTxId_, addr, &lastConfirmedNode);
    sql2 = Strings::Format("`total_received`=%lld,`balance_final`=%lld",
                           lastConfirmedNode.totalReceived_ + (node->balanceDiff_ > 0 ? node->balanceDiff_ : 0),
                           lastConfirmedNode.balanceFinal_ + node->balanceDiff_);
  } else {
    sql2 = Strings::Format("`total_received`=%lld,`balance_final`=%lld",
                           (node->balanceDiff_ > 0 ? node->balanceDiff_ : 0),
                           node->balanceDiff_);
    assert(node->balanceDiff_ > 0);  // 首笔交易必须是进账
  }

  //
  // 更新 address_txs_<yyyymm>.tx_height
  //
  sql = Strings::Format("UPDATE `address_txs_%d` SET `tx_height`=%d,%s "
                        " WHERE `address_id`=%lld AND `tx_id`=%lld ",
                        tableIdx_AddrTxs(node->ymd_), height, sql2.c_str(),
                        node->addressId_, node->txId_);
  dbExplorer_.updateOrThrowEx(sql, 1);

  //
  // 变更地址未确认额度等信息
  //
  const int64_t received = (node->balanceDiff_ > 0 ? node->balanceDiff_ : 0);
  const int64_t sent     = (node->balanceDiff_ < 0 ? node->balanceDiff_ * -1 : 0);

  sql = Strings::Format("UPDATE `addresses_%04d` SET "
                        " `unconfirmed_received` = `unconfirmed_received` - %lld,"
                        " `unconfirmed_sent`     = `unconfirmed_sent`     - %lld,"
                        " `unconfirmed_tx_count` = `unconfirmed_tx_count` - 1, "
                        " `last_confirmed_tx_ymd`=%d, `last_confirmed_tx_id`=%lld, "
                        " `updated_at`='%s' WHERE `id`=%lld ",
                        tableIdx_Addr(addr->addrId_),
                        received, sent, node->ymd_, node->txId_,
                        date("%F %T").c_str(), addr->addrId_);
  dbExplorer_.updateOrThrowEx(sql, 1);

  addr->unconfirmedReceived_ -= received;
  addr->unconfirmedSent_     -= sent;
  addr->unconfirmedTxCount_--;
  addr->lastConfirmedTxId_  = node->txId_;
  addr->lastConfirmedTxYmd_ = node->ymd_;
}



// 确认一个交易
void Parser::confirmTx(class TxLog2 *txLog2) {
  //
  // confirm: 变更为确认（设定块高度值等），若不是紧接着上个已确认的交易，则调整交易链。
  //          若该交易涉及日期变更（从上月末变更为下月初），则需要移动其后所有的tx至新日期表中
  //
  string sql;

  // 拿到关联地址的余额变更记录，可能需要调整交易链
  auto addressBalance = _getTxAddressBalance(txLog2->txId_, txLog2->txHash_, txLog2->tx_);

  for (auto &it : *addressBalance) {
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
      // 当前节点交换至第一个未确认节点
      _confirmTx_MoveToFirstUnconfirmed(addr, &currNode);
    }
    assert(currNode.ymd_ == UNCONFIRM_TX_YMD);

    //
    // 移动数据节点，从 UNCONFIRM_TX_YMD 至 txLog2->ymd_
    //
    _updateTxNodeYmd(addr, &currNode, txLog2->ymd_);
    _getAddressTxNode(txLog2->txId_, addr, &currNode);
    assert(currNode.ymd_ == txLog2->ymd_);

    //
    // 确认
    //
    _confirmAddressTxNode(&currNode, addr, txLog2->blkHeight_);
  } /* /for */

  //
  // table.txs_xxxx
  //
  string feeSql;
  if (txLog2->tx_.IsCoinBase()) {
    // confirm时知道高度值，重新计算 coinbase tx fee
    const int64_t fee = txLog2->tx_.GetValueOut() - GetBlockValue(txLog2->blkHeight_, 0);
    feeSql = Strings::Format(",fee=%lld ", fee);
  }
  sql = Strings::Format("UPDATE `txs_%04d` SET `height`=%d,`ymd`=%d %s WHERE `tx_id`=%lld ",
                        tableIdx_Tx(txLog2->txId_), txLog2->blkHeight_, txLog2->ymd_,
                        feeSql.c_str(), txLog2->txId_);
  dbExplorer_.updateOrThrowEx(sql, 1);

  // 处理未确认计数器和记录
  removeUnconfirmedTxPool(txLog2);

  // cache
  removeAddressCache(*addressBalance, txLog2->ymd_);
  removeTxCache(txLog2->txHash_);
}

// unconfirm tx (address node)
void Parser::_unconfirmAddressTxNode(AddressTxNode *node, LastestAddressInfo *addr) {
  string sql;

  //
  // 更新 address_txs_<yyyymm>.tx_height
  //
  sql = Strings::Format("UPDATE `address_txs_%d` SET `tx_height`=0,"
                        " `total_received`=0,`balance_final`=0 "
                        " WHERE `address_id`=%lld AND `tx_id`=%lld ",
                        tableIdx_AddrTxs(node->ymd_),
                        node->addressId_, node->txId_);
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
                        " `unconfirmed_tx_count` = `unconfirmed_tx_count` + 1,  "
                        " `last_confirmed_tx_ymd`=%d, `last_confirmed_tx_id`=%lld, "
                        " `updated_at`='%s' WHERE `id`=%lld ",
                        tableIdx_Addr(addr->addrId_),
                        received, sent,
                        (node->prevTxId_ ? prevNode.ymd_  : 0),
                        (node->prevTxId_ ? prevNode.txId_ : 0),
                        date("%F %T").c_str(), addr->addrId_);
  dbExplorer_.updateOrThrowEx(sql, 1);

  addr->unconfirmedReceived_ += received;
  addr->unconfirmedSent_     += sent;
  addr->unconfirmedTxCount_++;
  addr->lastConfirmedTxId_  = (node->prevTxId_ ? prevNode.txId_ : 0);
  addr->lastConfirmedTxYmd_ = (node->prevTxId_ ? prevNode.ymd_  : 0);
}

// 取消交易的确认
void Parser::unconfirmTx(class TxLog2 *txLog2) {
  //
  // unconfirm: 解除最后一个已确认的交易（重置块高度零等），必然是最近确认交易，不涉及调整交易链
  //
  string sql;

  // 拿到关联地址的余额变更记录，可能需要调整交易链
  auto addressBalance = _getTxAddressBalance(txLog2->txId_, txLog2->txHash_, txLog2->tx_);

  for (auto &it : *addressBalance) {
    const int64_t addrID       = it.first;
    LastestAddressInfo *addr = _getAddressInfo(dbExplorer_, addrID);
    AddressTxNode currNode;
    _getAddressTxNode(txLog2->txId_, addr, &currNode);

    // 移动数据节点，从当前的至 UNCONFIRM_TX_YMD
    _updateTxNodeYmd(addr, &currNode, UNCONFIRM_TX_YMD);
    _getAddressTxNode(txLog2->txId_, addr, &currNode);
    assert(currNode.ymd_ == UNCONFIRM_TX_YMD);

    // 反确认
    _unconfirmAddressTxNode(&currNode, addr);
  } /* /for */

  // table.txs_xxxx，重置高度，不涉及变更YMD
  sql = Strings::Format("UPDATE `txs_%04d` SET `height`=0 WHERE `tx_id`=%lld ",
                        tableIdx_Tx(txLog2->txId_), txLog2->txId_);
  dbExplorer_.updateOrThrowEx(sql, 1);

  // 处理未确认计数器和记录
  addUnconfirmedTxPool(txLog2);

  // cache
  removeAddressCache(*addressBalance, txLog2->ymd_);
  removeTxCache(txLog2->txHash_);
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

  // 移除块时间戳
  blkTs_.popBlock();

  if (cacheEnable_) {
    // http://twiki.bitmain.com/bin/view/Main/SSDB-Cache
    cache_->insertKV(Strings::Format("blkh_%d", txLog2->blkHeight_));
    cache_->insertKV(Strings::Format("blk_%s",
                                     header.GetHash().ToString().c_str()));
  }
}

// 回滚，重新插入未花费记录
void _unremoveUnspentOutputs(MySQLConnection &db,
                             const int32_t blockHeight,
                             const int64_t txId, const int32_t position,
                             const int32_t ymd,
                             map<int64_t, int64_t> &addressBalance) {
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
}

// 回滚交易 inputs
static
void _rejectTxInputs(MySQLConnection &db,
                     class TxLog2 *txLog2,
                     const int32_t ymd,
                     map<int64_t, int64_t> &addressBalance) {
  const CTransaction &tx = txLog2->tx_;
  const int32_t blockHeight = txLog2->blkHeight_;
  const int64_t txId = txLog2->txId_;

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
    _unremoveUnspentOutputs(db, blockHeight, prevTxId, prevPos, ymd, addressBalance);
  } /* /for */

  // 删除 table.tx_inputs_xxxx 记录， coinbase tx 也有 tx_inputs_xxxx 记录
  sql = Strings::Format("DELETE FROM `tx_inputs_%04d` WHERE `tx_id`=%lld ",
                        txId % 100, txId);
  db.updateOrThrowEx(sql, (int32_t)tx.vin.size());
}

// 回滚交易 outputs
static
void _rejectTxOutputs(MySQLConnection &db,
                      class TxLog2 *txLog2, const int32_t ymd,
                      map<int64_t, int64_t> &addressBalance) {
  const CTransaction &tx = txLog2->tx_;
  const int64_t txId = txLog2->txId_;

  int n;
  const string now = date("%F %T");
  set<string> allAddresss;
  string sql;

  // 提取涉及到的所有地址
  n = -1;
  for (auto &out : tx.vout) {
    n++;
    txnouttype type;
    vector<CTxDestination> addresses;
    int nRequired;
    if (!ExtractDestinations(out.scriptPubKey, type, addresses, nRequired)) {
      LOG_WARN("extract destinations failure, txId: %lld, hash: %s, position: %d",
               txId, tx.GetHash().ToString().c_str(), n);
      continue;
    }
    for (auto &addr : addresses) {  // multiSig 可能由多个输出地址
      allAddresss.insert(CBitcoinAddress(addr).ToString());
    }
  }
  // 拿到所有地址的id
  map<string, int64_t> addrMap;
  GetAddressIds(db, allAddresss, addrMap);

  // 处理输出
  // (`address_id`, `tx_id`, `position`, `position2`, `block_height`, `value`, `created_at`)
  n = -1;
  for (auto &out : tx.vout) {
    n++;
    txnouttype type;
    vector<CTxDestination> addresses;
    int nRequired;
    ExtractDestinations(out.scriptPubKey, type, addresses, nRequired);

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
                        " `unconfirmed_tx_count` = `unconfirmed_tx_count` - 1, "
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
  addr->unconfirmedTxCount_--;
  addr->endTxYmd_ = node->prevYmd_;
  addr->endTxId_  = node->prevTxId_;
}


// 回滚：变更地址&地址对应交易记录
void Parser::_rejectAddressTxs(class TxLog2 *txLog2,
                               const map<int64_t, int64_t> &addressBalance) {
  for (auto &it : addressBalance) {
    const int64_t addrID = it.first;
    //
    // 当前要reject的记录不是最后一条记录：需要移动节点
    //
    LastestAddressInfo *addr = _getAddressInfo(dbExplorer_, addrID);
    AddressTxNode currNode;  // 当前节点

    while (1) {
      _getAddressTxNode(txLog2->txId_, addr, &currNode);

      // 涉及移动节点的情形: 当前节点并非最后一个节点
      if (addr->endTxId_ == currNode.txId_) {
        break;
      }

      // 将本节点交换至交易链末尾
      _rejectTx_MoveToLastUnconfirmed(addr, &currNode);
    }
    assert(addr->endTxId_ == currNode.txId_);

    // 移除本节点 (含地址信息变更)
    _removeAddressTxNode(addr, &currNode);
  } /* /for */
}


static
void _rejectTx(MySQLConnection &db, class TxLog2 *txLog2) {
  string sql = Strings::Format("DELETE FROM `txs_%04d` WHERE `tx_id`=%lld ",
                               tableIdx_Tx(txLog2->txId_), txLog2->txId_);
  db.updateOrThrowEx(sql, 1);
}

static
int32_t _getTxYmd(MySQLConnection &db, const int64_t txId) {
  MySQLResult res;
  string sql = Strings::Format("SELECT `ymd` FROM `txs_%04d` WHERE `tx_id`=%lld ",
                               tableIdx_Tx(txId), txId);
  db.query(sql, res);
  assert(res.numRows() == 1);
  char **row = res.nextRow();
  return atoi(row[0]);
}

// 回滚一个交易, reject 的交易都是未确认的交易
void Parser::rejectTx(class TxLog2 *txLog2) {
  map<int64_t, int64_t> addressBalance;
  const int32_t ymd = _getTxYmd(dbExplorer_, txLog2->txId_);

  _rejectTxInputs  (dbExplorer_, txLog2, ymd, addressBalance);
  _rejectTxOutputs (dbExplorer_, txLog2, ymd, addressBalance);
  _rejectAddressTxs(txLog2, addressBalance);
  _rejectTx        (dbExplorer_, txLog2);

  // 处理未确认计数器和记录
  removeUnconfirmedTxPool(txLog2);

  // cache
  removeAddressCache(addressBalance, ymd);
  if (!txLog2->tx_.IsCoinBase()) {
    for (auto &in : txLog2->tx_.vin) {
      removeTxCache(in.prevout.hash);
    }
  }
  removeTxCache(txLog2->txHash_);
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

bool Parser::tryFetchTxLog2(class TxLog2 *txLog2, const int64_t lastId) {
  MySQLResult res;
  char **row = nullptr;
  string sql;

  // batch_id 为 -1 表示最后的临时记录
  sql = Strings::Format(" SELECT `id`,`type`,`block_height`,`block_id`, "
                        " `max_block_timestamp`, `tx_hash`,`created_at` "
                        " FROM `0_txlogs2` "
                        " WHERE `id` > %lld AND `batch_id` <> -1 ORDER BY `id` ASC LIMIT 1 ",
                        lastId);

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

  //
  // accept: 我们把未确认的交易都设置为未来时间: 2030-01-01
  //
  if (txLog2->type_ == LOG2TYPE_TX_ACCEPT) {
    txLog2->ymd_ = UNCONFIRM_TX_YMD;
    txLog2->maxBlkTimestamp_ = UNCONFIRM_TX_TIMESTAMP;
  }

  LOG_INFO("process txlog2(%c), logId: %d, type: %d, "
           "height: %d, tx hash: %s, created: %s",
           '+',
           txLog2->id_, txLog2->type_, txLog2->blkHeight_,
           txLog2->txHash_.ToString().c_str(), txLog2->createdAt_.c_str());

  // find raw tx hex & id
  {
    txInfoCache_.getTxInfo(dbExplorer_, txLog2->txHash_,
                           &(txLog2->txId_), &(txLog2->txHex_));
    if (!DecodeHexTx(txLog2->tx_, txLog2->txHex_)) {
      THROW_EXCEPTION_DBEX("TX decode failed, hex: %s", txLog2->txHex_.c_str());
    }
  }

  // 清理旧记录: 保留50万条已经消费过的记录，每2万条触发清理一次
  if (txLog2->id_ > 50*10000 && txLog2->id_ % 20000 == 0) {
    string delSql = Strings::Format("DELETE FROM `0_txlogs2` WHERE `id` < %lld",
                                    txLog2->id_ - 50*10000);
    const size_t delRowNum = dbExplorer_.update(delSql);
    LOG_INFO("delete expired txlogs2 items: %llu", delRowNum);
  }

  return true;
}

