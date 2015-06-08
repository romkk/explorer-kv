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

#include "Parser.h"
#include "Common.h"
#include "Util.h"

#include "bitcoin/base58.h"
#include "bitcoin/util.h"

std::unordered_map<int64_t/*address ID*/, LastestAddressInfo *> gAddrTxCache;

RawBlock::RawBlock(const int64_t blockId, const int32_t height, const int32_t chainId,
                   const uint256 hash, const string &hex) {
  blockId_ = blockId;
  height_  = height;
  chainId_ = chainId;
  hash_    = hash;
  hex_     = hex;
}

LastestAddressInfo::LastestAddressInfo(int32_t beginTxYmd, int32_t endTxYmd,
                                       int64_t beginTxId, int64_t endTxId,
                                       int64_t totalReceived, int64_t totalSent,
                                       int64_t txCount) {
  beginTxYmd_    = beginTxYmd;
  endTxYmd_      = endTxYmd;
  beginTxId_     = beginTxId;
  endTxId_       = endTxId;
  totalReceived_ = totalReceived;
  totalSent_     = totalSent;
  txCount_       = txCount;
}

// 获取block raw hex/id/chainId...
void _getBlockRawHex(const int32_t height,
                     MySQLConnection &db,
                     string *rawHex, int32_t *chainId, int64_t *blockId) {
  string sql;

  // 从DB获取raw block
  MySQLResult res;
  sql = Strings::Format("SELECT `hex`,`chain_id`,`id` FROM `0_raw_blocks`  "
                        " WHERE `block_height` = %d AND `chain_id`=0 ",
                        height);

  char **row = nullptr;
  db.query(sql, res);
  if (res.numRows() == 0) {
    THROW_EXCEPTION_DBEX("can't find block by height from DB: %d", height);
  }

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
}

// 获取block raw hex/id/chainId...
// Rollback时用该函数
void _getBlockRawHexRollback(const int32_t height,
                             MySQLConnection &db,
                             string *rawHex, int32_t *chainId, int64_t *blockId) {
  string sql;
  MySQLResult res;
  char **row = nullptr;
  string hash;

  // 0_blocks 获取此高度的hash，不可从 0_row_blocks 中获取，因为此时分叉了
  // 0_row_blocks 可能拿到的该高度另一个分支的块
  sql = Strings::Format("SELECT `hash` FROM `0_blocks` WHERE "
                        " `height` = %d AND `chain_id`=0 ", height);
  db.query(sql, res);
  if (res.numRows() == 0) {
    THROW_EXCEPTION_DBEX("can't find block by height from DB when rollback: %d",
                         height);
  }
  row = res.nextRow();
  hash = string(row[0]);

  // 根据 hash 从 0_row_blocks 中获取
  sql = Strings::Format("SELECT `hex`,`chain_id`,`id` FROM `0_raw_blocks`  "
                        " WHERE `block_hash` = '%s' ", hash.c_str());
  db.query(sql, res);
  if (res.numRows() == 0) {
    THROW_EXCEPTION_DBEX("can't find block from DB when rollback: %s",
                         hash.c_str());
  }
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
}



// 批量插入函数
bool multiInsert(MySQLConnection &db, const string &table,
                 const string &fields, const vector<string> &values) {
  string sqlPrefix = Strings::Format("INSERT INTO `%s`(%s) VALUES ",
                                     table.c_str(), fields.c_str());

  if (values.size() == 0 || fields.length() == 0 || table.length() == 0) {
    return false;
  }

  string sql = sqlPrefix;
  for (auto &it : values) {
    sql += Strings::Format("(%s),", it.c_str());
    // 超过 8MB
    if (sql.length() > 8*1024*1024) {
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
TxLog::TxLog():logId_(0), tableIdx_(-1), status_(0), type_(0), blkHeight_(-2), txId_(0) {
}
TxLog::~TxLog() {}


//// 若SSDB宕机，则丢弃数据
//class CacheManager {
//  ssdb::Client *ssdb_;
//  mutex lock_;
//
//  // 待删除队列
//  vector<string> qkv_;
//  vector<string> qzset_;
//  vector<std> qhashset_;
//
//  void threadConsumer();
//
//public:
//  CacheManager();
//  ~CacheManager();
//
//  bool tryOpen();
//
//  void insertKV(const string &key);
//  void insertZSet(const string &key);
//  void insertHashSet(const string &key);
//};

CacheManager::CacheManager(const string  SSDBHost, const int32_t SSDBPort):
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
  qkvTemp_.insert(key);
  LOG_DEBUG("[CacheManager::insertKV] key: %s", key.c_str());
}

void CacheManager::insertHashSet(const string &address,
                                 const string &tableName) {
  qhashsetTemp_.push_back(std::make_pair(address, tableName));
  LOG_DEBUG("[CacheManager::insertHashSet] address:%s, tableName: %d",
            address.c_str(), tableName.c_str());
}

void CacheManager::commit() {
  ScopeLock sl(lock_);

  // 超过500万，丢弃数据，防止撑爆内存
  if (qkv_.size() > 500 * 10000) {
    vector<string>().swap(qkv_);
  }
  for (auto &it : qkvTemp_) {
    qkv_.push_back(it);
  }
  qkvTemp_.clear();

  // 超过500万，丢弃数据，防止撑爆内存
  if (qhashset_.size() > 500 * 10000) {
    vector<std::pair<string, string> >().swap(qhashset_);
  }
  for (auto &it : qhashsetTemp_) {
    qhashset_.push_back(std::make_pair(it.first, it.second));
  }
  qhashsetTemp_.clear();
}

//
// SSDB数据结构文档：http://twiki.bitmain.com/bin/view/Main/SSDB-Cache
//
void CacheManager::threadConsumer() {
  vector<string> buf;
  vector<std::pair<string, string> > bufHashSet;
  buf.reserve(512);
  bufHashSet.reserve(512);
  ssdb::Status s;
  int64_t successCount, itemCount;

  while (running_) {
    assert(ssdbAlive_ == true);
    assert(ssdb_ != nullptr);
    itemCount = successCount = 0;

    // Key - Value
    {
      ScopeLock sl(lock_);
      buf = qkv_;
      qkv_.clear();
    }
    if (buf.size()) {
      itemCount += buf.size();
      s = ssdb_->multi_del(buf);
      LOG_DEBUG("ssdb multi del, count: %lld", buf.size());
      if (s.error()) {
        LOG_ERROR("ssdb KV del fail");
      } else {
        successCount += buf.size();
      }
      buf.clear();
    }

    // HashSet: 目前HashSet类型就一个: addr_table_{addr_id}
    {
      ScopeLock sl(lock_);
      bufHashSet = qhashset_;
      qhashset_.clear();
    }
    if (bufHashSet.size()) {
      itemCount += bufHashSet.size();
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
      bufHashSet.clear();
    }

    if (itemCount != successCount) {
      LOG_ERROR("ssdb some items failure, total: %lld, success: %lld",
                itemCount, successCount);
    }

    if (itemCount == 0) {
      sleepMs(200);  // 暂无数据
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



Parser::Parser():dbExplorer_(Config::GConfig.get("db.explorer.uri")),
running_(true), cache_(nullptr)
{
  // setup cache manager: SSDB
  cacheEnable_ = Config::GConfig.getBool("ssdb.enable", false);
  if (cacheEnable_) {
    cache_ = new CacheManager(Config::GConfig.get("ssdb.host"),
                              (int32_t)Config::GConfig.getInt("ssdb.port"));
  }
}

Parser::~Parser() {
  stop();

  if (cache_ != nullptr) {
    delete cache_;
    cache_ = nullptr;
  }
}

void Parser::stop() {
  if (running_) {
    running_ = false;
    LOG_INFO("stop tparser");
  }
}

bool Parser::init() {
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

  return true;
}


void Parser::run() {
  TxLog txlog;
  int64_t lastTxLogOffset;

  while (running_) {
    lastTxLogOffset = getLastTxLogOffset();

    if (tryFetchLog(&txlog, lastTxLogOffset) == false) {
      sleepMs(1000);
      continue;
    }

    checkTableAddressTxs(txlog.blockTimestamp_);

    if (!dbExplorer_.execute("START TRANSACTION")) {
      goto error;
    }

    if (txlog.type_ == TXLOG_TYPE_ACCEPT) {
      if (txlog.tx_.IsCoinBase()) {
        acceptBlock(txlog.blkHeight_);
      }
      acceptTx(&txlog);
    }
    else if (txlog.type_ == TXLOG_TYPE_ROLLBACK) {
      if (txlog.tx_.IsCoinBase()) {
      	rollbackBlock(txlog.blkHeight_);
      }
      rollbackTx(&txlog);
    }
    else {
      LOG_FATAL("invalid txlog type: %d", txlog.type_);
    }

    // 设置为当前的ID，该ID不一定连续
    updateLastTxlogId(lastTxLogOffset / 100000000 + txlog.logId_);

    if (!dbExplorer_.execute("COMMIT")) {
      goto error;
    }

    if (cache_ != nullptr) {
      cache_->commit();
    }
  } /* /while */
  return;

error:
  dbExplorer_.execute("ROLLBACK");
  return;
}

// 检测表是否存在，`create table` 这样的语句会导致DB事务隐形提交，必须摘离出事务之外
void Parser::checkTableAddressTxs(const uint32_t timestamp) {
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

// 获取当前txlogs的最大表索引
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

void Parser::updateLastTxlogId(const int64_t newId) {
  string sql = Strings::Format("UPDATE `0_explorer_meta` SET `value` = '%lld',`updated_at`='%s' "
                               " WHERE `key`='jiexi.last_txlog_offset'",
                               newId, date("%F %T").c_str());
  if (!dbExplorer_.execute(sql.c_str())) {
    THROW_EXCEPTION_DBEX("failed to update 'jiexi.last_txlog_offset' to %lld",
                         newId);
  }
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

  // 将当前高度的块的chainID增一，若存在分叉的话. UNIQUE	(height, chain_id)
  // 当前即将插入的块的chainID必然为0
  sql = Strings::Format("UPDATE `0_blocks` SET `chain_id`=`chain_id`+1 "
                        " WHERE `height` = %d ", height);
  db.update(sql.c_str());

  // 构造插入SQL
  sql = Strings::Format("SELECT `block_id` FROM `0_blocks` WHERE `block_id`=%lld",
                        blockId);
  db.query(sql, res);
  if (res.numRows() == 0) {  // 不存在则插入，由于有rollback等行为，块由可能已经存在了
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
                                  " %lld, %d, '%s', %d, '%s', %u, "
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
                                  (uint32_t)header.nTime,
                                  // 2.
                                  header.nBits, header.nNonce, prevBlockId, prevBlockHash.c_str(),
                                  // 3.
                                  0/* chainId */, blockBytes,
                                  // 4.
                                  difficulty, blk.vtx.size(), rewardBlock, rewardFees, date("%F %T").c_str());
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
void Parser::acceptBlock(const int32_t height) {
  // 获取块Raw Hex
  string blkRawHex;
  int32_t chainId;
  int64_t blockId;

  _getBlockRawHex(height, dbExplorer_, &blkRawHex, &chainId, &blockId);
  assert(chainId == 0);

  // 解码Raw Hex
  vector<unsigned char> blockData(ParseHex(blkRawHex));
  CDataStream ssBlock(blockData, SER_NETWORK, BITCOIN_PROTOCOL_VERSION);
  CBlock blk;
  try {
    ssBlock >> blk;
  }
  catch (std::exception &e) {
    THROW_EXCEPTION_DBEX("Block decode failed, height: %d, blockId: %lld",
                         height, blockId);
  }

  // 拿到 tx_hash -> tx_id 的对应关系
  // 要求"导入"模块：存入所有的区块交易(table.raw_txs_xxxx)
  std::set<uint256> hashVec;
  std::map<uint256, int64_t> hash2id;
  for (auto & it : blk.vtx) {
    hashVec.insert(it.GetHash());
  }
  txsHash2ids(hashVec, hash2id);

  // 插入数据至 table.0_blocks
  _insertBlock(dbExplorer_, blk, blockId, height, (int32_t)blkRawHex.length()/2);

  // 插入数据至 table.block_txs_xxxx
  _insertBlockTxs(dbExplorer_, blk, blockId, hash2id);
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

void _insertTxInputs(MySQLConnection &db, CacheManager *cache,
                     const CTransaction &tx,
                     const int64_t txId, map<int64_t, int64_t> &addressBalance,
                     int64_t &valueIn) {
  int n;
  const string tableName = Strings::Format("tx_inputs_%04d", txId % 100);
  const string now = date("%F %T");
  // table.tx_inputs_xxxx
  const string fields = "`tx_id`, `position`, `input_script_asm`, `input_script_hex`,"
  " `sequence`, `prev_tx_id`, `prev_position`, `prev_value`,"
  " `prev_address`, `prev_address_ids`, `created_at`";
  vector<string> values;
  string sql;

  n = -1;
  for (auto &in : tx.vin) {
    n++;
    uint256 prevHash;
    int64_t prevTxId;
    int32_t prevPos;

    if (tx.IsCoinBase()) {
      prevHash = 0;
      prevTxId = 0;
      prevPos  = -1;

      // 插入当前交易的inputs, coinbase tx的 scriptSig 不做decode，可能含有非法字符
      // 通常无法解析成功
      // coinbase无法担心其长度，bitcoind对coinbase tx的coinbase字段长度做了限制
      values.push_back(Strings::Format("%lld,%d,'','%s',%u,"
                                       "0,-1,0,'','','%s'",
                                       txId, n,
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
                            prevTxId % 100, txId, n,
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
                                       txId, n,
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
        // 遍历inputs所关联tx，全部删除： txid_{id}, tx_{hash}
        cache->insertKV(Strings::Format("txid_%lld", prevTxId));
        cache->insertKV(Strings::Format("tx_%s",     prevHash.ToString().c_str()));

        // 清理所有输入地址
        if (dbTxOutput.address.length()) {
          auto addrStrVec = split(dbTxOutput.address, '|');
          for (auto &singleAddrStr : addrStrVec) {
            cache->insertKV(Strings::Format("addr_%s", singleAddrStr.c_str()));
          }
        }
      }
    }
  } /* /for */

  // 执行插入 inputs
  if (!multiInsert(db, tableName, fields, values)) {
    THROW_EXCEPTION_DBEX("insert inputs fail, txId: %lld, hash: %s",
                         txId, tx.GetHash().ToString().c_str());
  }
}


void _insertTxOutputs(MySQLConnection &db, CacheManager *cache,
                      const CTransaction &tx,
                      const int64_t txId, const int64_t blockHeight,
                      map<int64_t, int64_t> &addressBalance) {
  int n;
  const string now = date("%F %T");
  set<string> allAddresss;

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
      const string addrStr = CBitcoinAddress(addr).ToString();
      allAddresss.insert(CBitcoinAddress(addr).ToString());
    }
  }
  // 拿到所有地址的id
  map<string, int64_t> addrMap;
  GetAddressIds(db, allAddresss, addrMap);

  for (auto &singleAddrStr : allAddresss) {
    cache->insertKV(Strings::Format("addr_%s", singleAddrStr.c_str()));
  }

  // 处理输出
  // (`address_id`, `tx_id`, `position`, `position2`, `block_height`, `value`, `created_at`)
  n = -1;
  vector<string> itemValues;
  for (auto &out : tx.vout) {
    n++;
    string addressStr;
    string addressIdsStr;
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
      addressStr    += addrStr + ",";
      addressIdsStr += Strings::Format("%lld", addrId) + ",";

      // 增加每个地址的余额
      addressBalance[addrId] += out.nValue;

      // 每一个输出地址均生成一条 address_unspent_outputs 记录
      string sql = Strings::Format("INSERT INTO `address_unspent_outputs_%04d`"
                                   " (`address_id`, `tx_id`, `position`, `position2`, `block_height`, `value`, `created_at`)"
                                   " VALUES (%lld, %lld, %d, %d, %lld, %lld, '%s') ",
                                   addrId % 10, addrId, txId, n, i, blockHeight,
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
                                         txId, n, addressStr.c_str(), addressIdsStr.c_str(),
                                         // `value`,`output_script_asm`,`output_script_hex`,`output_script_type`
                                         out.nValue,
                                         outputScriptAsm.c_str(),
                                         outputScriptHex.length() < 32*1024*1024 ? outputScriptHex.c_str() : "",
                                         GetTxnOutputType(type) ? GetTxnOutputType(type) : "",
                                         // `spent_tx_id`,`spent_position`,`created_at`,`updated_at`
                                         now.c_str(), now.c_str()));
  }

  // table.tx_outputs_xxxx
  const string tableNameTxOutputs = Strings::Format("tx_outputs_%04d", txId % 100);
  const string fieldsTxOutputs = "`tx_id`,`position`,`address`,`address_ids`,`value`,"
  "`output_script_asm`,`output_script_hex`,`output_script_type`,"
  "`spent_tx_id`,`spent_position`,`created_at`,`updated_at`";
  // multi insert outputs
  if (!multiInsert(db, tableNameTxOutputs, fieldsTxOutputs, itemValues)) {
    THROW_EXCEPTION_DBEX("insert outputs fail, txId: %lld, hash: %s",
                         txId, tx.GetHash().ToString().c_str());
  }
}


// 变更地址&地址对应交易记录
void _insertAddressTxs(MySQLConnection &db, class TxLog *txLog,
                       const map<int64_t, int64_t> &addressBalance) {
  MySQLResult res;
  char **row;
  string sql;

  // 前面步骤会确保该表已经存在
  const int32_t ymd = atoi(date("%Y%m%d", txLog->blockTimestamp_).c_str());

  for (auto &it : addressBalance) {
    const int64_t addrID      = it.first;
    const int64_t balanceDiff = it.second;
    const string addrTableName = Strings::Format("addresses_%04d", tableIdx_Addr(addrID));
    std::unordered_map<int64_t, LastestAddressInfo *>::iterator it2;

    // 获取地址信息
    int32_t beginTxYmd, prevTxYmd;
    int64_t beginTxID, prevTxId, totalRecv, finalBalance, txCount;

    it2 = gAddrTxCache.find(addrID);
    if (it2 != gAddrTxCache.end()) {
      prevTxYmd    = it2->second->endTxYmd_;
      prevTxId     = it2->second->endTxId_;
      beginTxID    = it2->second->beginTxId_;
      beginTxYmd   = it2->second->beginTxYmd_;
      totalRecv    = it2->second->totalReceived_;
      finalBalance = it2->second->totalReceived_ - it2->second->totalSent_;
      txCount      = it2->second->txCount_;
    } else {
      sql = Strings::Format("SELECT `end_tx_ymd`,`end_tx_id`,`total_received`,"
                            " `total_sent`,`begin_tx_id`,`begin_tx_ymd`,`tx_count` "
                            " FROM `%s` WHERE `id`=%lld ",
                            addrTableName.c_str(), addrID);
      db.query(sql, res);
      if (res.numRows() != 1) {
        THROW_EXCEPTION_DBEX("can't find address record, addrId: %lld", addrID);
      }
      row = res.nextRow();

      prevTxYmd   = atoi(row[0]);
      prevTxId    = atoi64(row[1]);
      totalRecv   = atoi64(row[2]);
      finalBalance = totalRecv - atoi64(row[3]);
      beginTxID   = atoi64(row[4]);
      beginTxYmd  = atoi(row[5]);
      txCount     = atoi64(row[6]);

      auto ptr = new LastestAddressInfo(beginTxYmd, prevTxYmd, beginTxID,
                                        prevTxId, atoi64(row[2]), atoi64(row[3]),
                                        txCount);
      if (gAddrTxCache.size() > 100 * 10000) {
        // clear, 数量限制，防止长时间运行后占用过多内存
        std::unordered_map<int64_t, LastestAddressInfo *>().swap(gAddrTxCache);
      }
      gAddrTxCache[addrID] = ptr;
    }
    if ((it2 = gAddrTxCache.find(addrID)) == gAddrTxCache.end()) {
      THROW_EXCEPTION_DB("gAddrTxCache fatal");
    }

    // 处理前向记录
    if (prevTxYmd > 0 && prevTxId > 0) {
      // 更新前向记录
      sql = Strings::Format("UPDATE `address_txs_%d` SET `next_ymd`=%d, `next_tx_id`=%lld "
                            " WHERE `address_id`=%lld AND `tx_id`=%lld ",
                            tableIdx_AddrTxs(prevTxYmd), ymd, txLog->txId_, addrID, prevTxId);
      db.updateOrThrowEx(sql, 1);
    }
    assert(finalBalance + balanceDiff >= 0);

    // 插入当前记录
    sql = Strings::Format("INSERT INTO `address_txs_%d` (`address_id`, `tx_id`, `tx_height`,"
                          " `total_received`, `balance_diff`, `balance_final`, `idx`, "
                          " `prev_ymd`, `prev_tx_id`, `next_ymd`, `next_tx_id`, `created_at`)"
                          " VALUES (%lld, %lld, %d, %lld, %lld, %lld, %lld,"
                          "         %d, %lld, 0, 0, '%s') ",
                          tableIdx_AddrTxs(ymd), addrID, txLog->txId_, txLog->blkHeight_,
                          totalRecv + (balanceDiff > 0 ? balanceDiff : 0),
                          balanceDiff, finalBalance + balanceDiff,
                          txCount + 1,
                          prevTxYmd, prevTxId, date("%F %T").c_str());
    db.updateOrThrowEx(sql, 1);

    // 更新地址信息
    string sqlBegin = "";  // 是否更新 `begin_tx_ymd`/`begin_tx_id`
    if (beginTxYmd == 0) {
      assert(beginTxID  == 0);
      sqlBegin = Strings::Format("`begin_tx_ymd`=%d, `begin_tx_id`=%lld,",
                                 ymd, txLog->txId_);
      it2->second->beginTxId_  = txLog->txId_;
      it2->second->beginTxYmd_ = ymd;
    }
    sql = Strings::Format("UPDATE `%s` SET `tx_count`=`tx_count`+1, "
                          " `total_received` = `total_received` + %lld,"
                          " `total_sent`     = `total_sent`     + %lld,"
                          " `end_tx_ymd`=%d, `end_tx_id`=%lld, %s "
                          " `updated_at`='%s' WHERE `id`=%lld ",
                          addrTableName.c_str(),
                          (balanceDiff > 0 ? balanceDiff : 0),
                          (balanceDiff < 0 ? balanceDiff * -1 : 0),
                          ymd, txLog->txId_, sqlBegin.c_str(),
                          date("%F %T").c_str(), addrID);
    db.updateOrThrowEx(sql, 1);

    it2->second->totalReceived_ += (balanceDiff > 0 ? balanceDiff : 0);
    it2->second->totalSent_     += (balanceDiff < 0 ? balanceDiff * -1 : 0);
    it2->second->endTxId_  = txLog->txId_;
    it2->second->endTxYmd_ = ymd;
    it2->second->txCount_++;

  } /* /for */
}

// 插入交易
void _insertTx(MySQLConnection &db, class TxLog *txLog, int64_t valueIn) {
  // (`tx_id`, `hash`, `height`, `is_coinbase`, `version`, `lock_time`, `size`, `fee`, `total_in_value`, `total_out_value`, `inputs_count`, `outputs_count`, `created_at`)
  const string tName = Strings::Format("txs_%04d", txLog->txId_ / BILLION);
  const CTransaction &tx = txLog->tx_;  // alias
  string sql;

  int64_t fee = 0;
  const int64_t valueOut = txLog->tx_.GetValueOut();
  if (txLog->tx_.IsCoinBase()) {
    // coinbase的fee为 block rewards
    fee = valueOut - GetBlockValue(txLog->blkHeight_, 0);
  } else {
    fee = valueIn - valueOut;
  }

  sql = Strings::Format("INSERT INTO `%s`(`tx_id`, `hash`, `height`,`block_timestamp`, `is_coinbase`,"
                        " `version`, `lock_time`, `size`, `fee`, `total_in_value`, "
                        " `total_out_value`, `inputs_count`, `outputs_count`, `created_at`)"
                        " VALUES (%lld, '%s', %d, %u, %d, %d, %u, %d, %lld, %lld, %lld, %d, %d, '%s') ",
                        tName.c_str(),
                        // `tx_id`, `hash`, `height`
                        txLog->txId_, txLog->txHash_.ToString().c_str(), txLog->blkHeight_, txLog->blockTimestamp_,
                        // `is_coinbase`, `version`, `lock_time`
                        txLog->tx_.IsCoinBase() ? 1 : 0, tx.nVersion, tx.nLockTime,
                        // `size`, `fee`, `total_in_value`, `total_out_value`
                        txLog->txHex_.length()/2, fee, valueIn, valueOut,
                        // `inputs_count`, `outputs_count`, `created_at`
                        tx.vin.size(), tx.vout.size(), date("%F %T").c_str());
  db.updateOrThrowEx(sql, 1);
}


// 接收一个新的交易
void Parser::acceptTx(class TxLog *txLog) {
  // 硬编码特殊交易处理
  //
  // 1. tx hash: d5d27987d2a3dfc724e359870c6644b40e497bdc0589a033220fe15429d88599
  // 该交易在两个不同的高度块(91812, 91842)中出现过
  // 91842块中有且仅有这一个交易
  //
  // 2. tx hash: e3bf3d07d4b0375638d5f1db5255fe07ba2c4cb067cd81b84ee974b6585fb468
  // 该交易在两个不同的高度块(91722, 91880)中出现过
  if ((txLog->blkHeight_ == 91842 &&
       txLog->txHash_ == uint256("d5d27987d2a3dfc724e359870c6644b40e497bdc0589a033220fe15429d88599")) ||
      (txLog->blkHeight_ == 91880 &&
       txLog->txHash_ == uint256("e3bf3d07d4b0375638d5f1db5255fe07ba2c4cb067cd81b84ee974b6585fb468"))) {
    LOG_WARN("ignore tx, height: %d, hash: %s",
             txLog->blkHeight_, txLog->txHash_.ToString().c_str());
    return;
  }


  // 交易的输入之和，遍历交易后才能得出
  int64_t valueIn = 0;

  // 同一个地址只能产生一条交易记录: address <-> tx <-> balance_diff
  map<int64_t/* addrID */, int64_t/*  balance diff */> addressBalance;

  // 处理inputs
  _insertTxInputs(dbExplorer_, cache_, txLog->tx_, txLog->txId_,
                  addressBalance, valueIn);

  // 处理outputs
  _insertTxOutputs(dbExplorer_, cache_, txLog->tx_, txLog->txId_,
                   txLog->blkHeight_, addressBalance);

  // 变更地址相关信息
  _insertAddressTxs(dbExplorer_, txLog, addressBalance);

  // 插入交易tx
  _insertTx(dbExplorer_, txLog, valueIn);
}


// 回滚一个块操作
void Parser::rollbackBlock(const int32_t height) {
  MySQLResult res;
  string sql;

  // 获取块Raw Hex
  string blkRawHex;
  int32_t chainId;
  int64_t blockId;

  _getBlockRawHexRollback(height, dbExplorer_, &blkRawHex, &chainId, &blockId);
  assert(chainId == 0);

  // 解码Raw Block
  vector<unsigned char> blockData(ParseHex(blkRawHex));
  CDataStream ssBlock(blockData, SER_NETWORK, BITCOIN_PROTOCOL_VERSION);
  CBlock blk;
  try {
    ssBlock >> blk;
  }
  catch (std::exception &e) {
    THROW_EXCEPTION_DBEX("Block decode failed, height: %d, blockId: %lld",
                         height, blockId);
  }
  const CBlockHeader header = blk.GetBlockHeader();  // alias
  const  string prevBlockHash = header.hashPrevBlock.ToString();

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
                        tableIdx_BlockTxs(blockId), blockId);
  dbExplorer_.updateOrThrowEx(sql, (int32_t)blk.vtx.size());

  if (cacheEnable_) {
    // http://twiki.bitmain.com/bin/view/Main/SSDB-Cache
    cache_->insertKV(Strings::Format("blkh_%d", height));
  }
}

// 回滚，重新插入未花费记录
void _unremoveUnspentOutputs(MySQLConnection &db, CacheManager *cache,
                             const int64_t blockHeight,
                             const int64_t txId, const int32_t position,
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
  const string s = string(row[0]);
  const int64_t value = atoi64(row[1]);
  const string ids = string(row[2]);

  vector<string> addressIdsStrVec = split(s, '|');
  n = -1;
  for (auto &addrIdStr : addressIdsStrVec) {
    n++;
    const int64_t addrId = atoi64(addrIdStr.c_str());
    addressBalance[addrId] += -1 * value;

    // 恢复每一个输出地址的 address_unspent_outputs 记录
    sql = Strings::Format("INSERT INTO `address_unspent_outputs_%04d`"
                          " (`address_id`, `tx_id`, `position`, `position2`, `block_height`, `value`, `created_at`)"
                          " VALUES (%lld, %lld, %d, %d, %lld, %lld, '%s') ",
                          addrId % 10, addrId, txId, position, n, blockHeight,
                          value, date("%F %T").c_str());
    db.updateOrThrowEx(sql, 1);
  }

  // 缓存
  if (cache) {
    auto addrStrVec = split(ids, '|');
    for (auto &singleAddrStr : addrStrVec) {
      cache->insertKV(Strings::Format("addr_%s", singleAddrStr.c_str()));
    }
  }
}

// 回滚交易 inputs
void _rollbackTxInputs(MySQLConnection &db, CacheManager *cache,
                       class TxLog *txLog,
                       map<int64_t, int64_t> &addressBalance) {
  const CTransaction &tx = txLog->tx_;
  const int32_t blockHeight = txLog->blkHeight_;
  const int64_t txId = txLog->txId_;

  string sql;
  int n = -1;
  for (auto &in : tx.vin) {
    n++;

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
    _unremoveUnspentOutputs(db, cache, blockHeight, prevTxId, prevPos, addressBalance);

    if (cache != nullptr) {
      // http://twiki.bitmain.com/bin/view/Main/SSDB-Cache
      // 遍历inputs所关联tx，全部删除： txid_{id}, tx_{hash}
      cache->insertKV(Strings::Format("txid_%lld", prevTxId));
      cache->insertKV(Strings::Format("tx_%s",     prevHash));
    }
  } /* /for */

  // 删除 table.tx_inputs_xxxx 记录
  sql = Strings::Format("DELETE FROM `tx_inputs_%04d` WHERE `tx_id`=%lld ",
                        txId % 100, txId);
  db.updateOrThrowEx(sql, (int32_t)tx.vin.size());
}

// 回滚交易 outputs
void _rollbackTxOutputs(MySQLConnection &db, CacheManager *cache,
                        class TxLog *txLog,
                        map<int64_t, int64_t> &addressBalance) {
  const CTransaction &tx = txLog->tx_;
  const int64_t txId = txLog->txId_;

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

      if (cache) {
        cache->insertKV(Strings::Format("addr_%s", addrStr.c_str()));
      }
    }
  }

  // delete table.tx_outputs_xxxx
  sql = Strings::Format("DELETE FROM `tx_outputs_%04d` WHERE `tx_id`=%lld",
                        txId % 100, txId);
  db.updateOrThrowEx(sql, (int32_t)tx.vout.size());
}


// 回滚：变更地址&地址对应交易记录
void _rollbackAddressTxs(MySQLConnection &db, class TxLog *txLog,
                         const map<int64_t, int64_t> &addressBalance) {
  MySQLResult res;
  char **row;
  string sql;

  // 前面步骤会确保该表已经存在
  const int32_t ymd = atoi(date("%Y%m%d", txLog->blockTimestamp_).c_str());

  for (auto &it : addressBalance) {
    const int64_t addrID      = it.first;
    const int64_t balanceDiff = it.second;

    // 获取地址信息
    string addrTableName = Strings::Format("addresses_%04d", tableIdx_Addr(addrID));
    sql = Strings::Format("SELECT `end_tx_ymd`,`end_tx_id`,(`total_received`-`total_sent`) "
                          " FROM `%s` WHERE `id`=%lld ",
                          addrTableName.c_str(), addrID);
    db.query(sql, res);
    assert(res.numRows() == 1);
    row = res.nextRow();

    const int32_t endTxYmd   = atoi(row[0]);
    const int64_t endTxId    = atoi64(row[1]);
    const int64_t curBalance = atoi64(row[2]);

    // 当前要回滚的记录应该就是最后一条记录
    if (endTxYmd != ymd || endTxId != txLog->txId_) {
      THROW_EXCEPTION_DBEX("address's last tx NOT match, addrId: %lld, "
                           "last ymd(%lld) should be %lld, "
                           "last txId(%lld) should be %lld",
                           addrID, endTxYmd, ymd, endTxId, txLog->txId_);
    }

    // 获取最后一条记录，提取其前向记录，倒数第二条记录，如果存在的话
    int64_t end2TxId = 0;
    int32_t end2TxYmd = 0;
    sql = Strings::Format("SELECT `prev_ymd`, `prev_tx_id` "
                          " FROM `address_txs_%d` WHERE `address_id`=%lld AND `tx_id`=%lld ",
                          tableIdx_AddrTxs(endTxYmd), addrID, endTxId);
    db.query(sql, res);
    if (res.numRows() == 1) {
      row = res.nextRow();
      end2TxYmd = atoi(row[0]);
      end2TxId  = atoi64(row[1]);
      if (end2TxYmd != 0) {
        assert(end2TxId != 0);
        // 设置倒数第二条记录 next 记录为空
        sql = Strings::Format("UPDATE `address_txs_%d` SET `next_ymd`=0, `next_tx_id`=0 "
                              " WHERE `address_id`=%lld AND `tx_id`=%lld ",
                              tableIdx_AddrTxs(end2TxYmd), addrID, end2TxId);
        db.updateOrThrowEx(sql, 1);
      }
    }

    // 删除最后一条记录
    sql = Strings::Format("DELETE FROM `address_txs_%d` WHERE `address_id`=%lld AND `tx_id`=%lld ",
                          tableIdx_AddrTxs(ymd), addrID, txLog->txId_);
    db.updateOrThrowEx(sql, 1);

    // 更新地址信息
    sql = Strings::Format("UPDATE `%s` SET `tx_count`=`tx_count`-1, "
                          " `total_received` = `total_received` - %lld,"
                          " `total_sent`     = `total_sent`     - %lld,"
                          " `end_tx_ymd`=%d, `end_tx_id`=%lld, %s `updated_at`='%s' "
                          " WHERE `id`=%lld ",
                          addrTableName.c_str(),
                          (balanceDiff > 0 ? balanceDiff : 0),
                          (balanceDiff < 0 ? balanceDiff * -1 : 0),
                          end2TxYmd, end2TxId,
                          // 没有倒数第二条，重置起始位置为空
                          end2TxYmd == 0 ? "`begin_tx_id`=0,`begin_tx_ymd`=0," : "",
                          date("%F %T").c_str(), addrID);
    db.updateOrThrowEx(sql, 1);

    // 清理地址的缓存
    gAddrTxCache.erase(addrID);

  } /* /for */
}

void _rollbackTx(MySQLConnection &db, CacheManager *cache, class TxLog *txLog) {
  string sql = Strings::Format("DELETE FROM `txs_%04d` WHERE `tx_id`=%lld ",
                               tableIdx_Tx(txLog->txId_), txLog->txId_);
  db.updateOrThrowEx(sql, 1);

  if (cache != nullptr) {
    // http://twiki.bitmain.com/bin/view/Main/SSDB-Cache
    // 删除自身: txid_{id}, tx_{hash}
    cache->insertKV(Strings::Format("txid_%lld", txLog->txId_));
    cache->insertKV(Strings::Format("tx_%s",     txLog->txHash_.ToString().c_str()));
  }
}

// 回滚一个交易
void Parser::rollbackTx(class TxLog *txLog) {
  // 同一个地址只能产生一条交易记录: address <-> tx <-> balance_diff
  map<int64_t, int64_t> addressBalance;

  _rollbackTxInputs  (dbExplorer_, cache_, txLog, addressBalance);
  _rollbackTxOutputs (dbExplorer_, cache_, txLog, addressBalance);
  _rollbackAddressTxs(dbExplorer_, txLog, addressBalance);
  _rollbackTx        (dbExplorer_, cache_, txLog);
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
    const string now = date("%F %T");
    sql = Strings::Format("INSERT INTO `0_explorer_meta`(`key`,`value`,`created_at`,`updated_at`) "
                          " VALUES('jiexi.last_txlog_offset', '0', '%s', '%s')",
                          now.c_str(), now.c_str());
    dbExplorer_.update(sql);
    lastTxLogOffset = 0;  // default value is zero
  }
  return lastTxLogOffset;
}

void _getRawTxFromDB(MySQLConnection &db, class TxLog *txLog) {
  MySQLResult res;
  string sql;
  char **row;
  const string txHashStr = txLog->txHash_.ToString();

  sql = Strings::Format("SELECT `hex`,`id` FROM `raw_txs_%04d` WHERE `tx_hash` = '%s' ",
                        HexToDecLast2Bytes(txHashStr) % 64, txHashStr.c_str());
  db.query(sql, res);
  if (res.numRows() == 0) {
    THROW_EXCEPTION_DBEX("can't find raw tx by hash: %s", txHashStr.c_str());
  }
  row = res.nextRow();
  txLog->txHex_ = string(row[0]);
  txLog->txId_  = atoi64(row[1]);
}

// 获取tx Hash/ID
void _getTxHexId(MySQLConnection &db, class TxLog *txLog) {
  _getRawTxFromDB(db, txLog);

  // parse hex string from parameter
  vector<unsigned char> txData(ParseHex(txLog->txHex_));
  CDataStream ssData(txData, SER_NETWORK, BITCOIN_PROTOCOL_VERSION);

  // deserialize binary data stream
  try {
    ssData >> txLog->tx_;
  }
  catch (std::exception &e) {
    THROW_EXCEPTION_DBEX("TX decode failed: %s", e.what());
  }
  if (txLog->tx_.GetHash() != txLog->txHash_) {
    THROW_EXCEPTION_DBEX("TX decode failed, hash is not match, db: %s, calc: %s",
                         txLog->txHash_.ToString().c_str(),
                         txLog->tx_.GetHash().ToString().c_str());
  }
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
                        "   `block_height`,`tx_hash`,`created_at`,`block_timestamp` "
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
  txLog->blockTimestamp_ = (uint32_t)atoi64(row[6]);

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
           tableNameIdx, txLog->logId_, txLog->type_, txLog->blkHeight_,
           txLog->txHash_.ToString().c_str(), txLog->createdAt_.c_str());

  // find raw tx hex/id
  _getTxHexId(dbExplorer_, txLog);

  return true;
}

