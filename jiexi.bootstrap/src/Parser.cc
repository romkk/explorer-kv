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

#include <pthread.h>

#include <boost/thread.hpp>

#include "Parser.h"
#include "Common.h"
#include "Util.h"

#include "bitcoin/base58.h"
#include "bitcoin/util.h"

static void _getRawTxFromDisk(const uint256 &hash, const int32_t height,
                              string *hex, int64_t *txId);

// global vars
AddrTxCache gAddrTxCache;


AddrTxCache::AddrTxCache() {
  pthread_mutex_init(&lock_, nullptr);
}

AddrTxCache::~AddrTxCache() {
  pthread_mutex_destroy(&lock_);
}

std::unordered_map<int64_t, LastestAddressInfo *>::const_iterator AddrTxCache::end() const {
  return cache_.end();
}

std::unordered_map<int64_t, LastestAddressInfo *>::iterator AddrTxCache::find(const int64_t addrId) {
  return cache_.find(addrId);
}

void AddrTxCache::insert(const int64_t addrId, LastestAddressInfo *ptr) {
  cache_.insert(std::make_pair(addrId, ptr));
}

bool AddrTxCache::lockAddr(const int64_t addrId) {
  bool res = false;
  pthread_mutex_lock(&lock_);
  if (using_[addrId] == false) {
    using_[addrId] = true;
    res = true;
  }
  pthread_mutex_unlock(&lock_);
  return res;
}

void AddrTxCache::unlockAddr(const int64_t addrId) {
  pthread_mutex_lock(&lock_);
  if (using_[addrId] == false) {
    THROW_EXCEPTION_DB("AddrTxCache unlockAddr failure, address is not locked");
  }
  using_[addrId] = false;
  pthread_mutex_unlock(&lock_);
}


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
                                       int64_t totalReceived, int64_t totalSent) {
  beginTxYmd_    = beginTxYmd;
  endTxYmd_      = endTxYmd;
  beginTxId_     = beginTxId;
  endTxId_       = endTxId;
  totalReceived_ = totalReceived;
  totalSent_     = totalSent;
}

BlockTx::BlockTx(int64_t txId, int32_t height, uint32_t nTime,
                 const uint256 &txHash, const CTransaction &tx, int32_t size) {
  txId_   = txId;
  height_ = height;
  nTime_  = nTime;
  txHash_ = txHash;
  tx_     = tx;
  txSize_ = size;
}

// 从磁盘读取 raw_block 批量文件
void _loadRawBlockFromDisk(map<int32_t, RawBlock*> &blkCache, const int32_t height) {
  string dir = Config::GConfig.get("cache.rawdata.dir", "");
  // 尾部添加 '/'
  if (dir.length() == 0) {
    dir = "./";
  }
  else if (dir[dir.length()-1] != '/') {
    dir += "/";
  }

  const int32_t height2 = (height / 10000) * 10000;
  string path = Strings::Format("%d_%d", height2, height2 + 9999);

  const string fname = Strings::Format("%s%s/0_raw_blocks",
                                       dir.c_str(), path.c_str());
  LOG_INFO("load raw block file: %s", fname.c_str());
  std::ifstream input(fname);
  std::string line;
  while (std::getline(input, line)) {
    auto arr = split(line, ',');
    // line: blockId, hash, height, chain_id, hex
    const uint256 blkHash(arr[1]);
    const int32_t blkHeight  = atoi(arr[2].c_str());
    const int32_t blkChainId = atoi(arr[3].c_str());

    blkCache[blkHeight] = new RawBlock(atoi64(arr[0].c_str()), blkHeight, blkChainId, blkHash, arr[4]);
  }
}

// 从文件读取raw block
void _getRawBlockFromDisk(const int32_t height, string *rawHex,
                          int32_t *chainId, int64_t *blockId) {
  // 从磁盘直接读取文件，缓存起来，减少数据库交互
  static map<int32_t, RawBlock*> blkCache;
  static int32_t heightBegin = -1;

  if (heightBegin != (height / 10000)) {
    // 载入数据
    LOG_INFO("try load raw block data from disk...");
    for (auto &it : blkCache) {
      delete it.second;
    }
    blkCache.clear();
    heightBegin = height / 10000;
    _loadRawBlockFromDisk(blkCache, height);
  }

  auto it = blkCache.find(height);
  if (it == blkCache.end()) {
    THROW_EXCEPTION_DBEX("can't find rawblock from disk cache, height: %d", height);
  }

  *rawHex  = it->second->hex_;
  *chainId = it->second->chainId_;
  *blockId = it->second->blockId_;
}

// 获取block raw hex/id/chainId...
void _getBlockRawHex(const int32_t height,
                     MySQLConnection &db,
                     string *rawHex, int32_t *chainId, int64_t *blockId) {
  // 从磁盘获取raw block
  _getRawBlockFromDisk(height, rawHex, chainId, blockId);
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


Parser::Parser():
 dbExplorer_(Config::GConfig.get("db.explorer.uri")),
 dbUri_(Config::GConfig.get("db.explorer.uri")),
 running_(true)
{
  threadsNumber_ = (int32_t)Config::GConfig.getInt("threads.number", 4);
  if (threadsNumber_ < 2) {
    THROW_EXCEPTION_DB("config option: 'threads.number' should >= 2");
  }
  poolBlockTx_.resize(threadsNumber_);
  for (int i = 0; i < threadsNumber_; i++) {
    poolDB_.push_back(new MySQLConnection(dbUri_));
  }
}

Parser::~Parser() {
  stop();
  for (int i = 0; i < threadsNumber_; i++) {
    delete poolDB_[i];
  }
  LOG_INFO("tparser stopped");
}

void Parser::stop() {
  if (running_) {
    running_ = false;
    LOG_INFO("stop tparser...");
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
  const int32_t maxCacheHeight = (int32_t)Config::GConfig.getInt("cache.raw.max.block.height", -1);

  int32_t height = getLastHeight();
  while (running_) {
    if (height + 1 > maxCacheHeight) {
      LOG_INFO("height(%d) is reach max height in raw file in disk", height, maxCacheHeight);
      stop();
      break;
    }
    height++;
    acceptBlock(height);
  }

  // 记录最后处理的高度
  updateLastHeight(height);
  return;

error:
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
  const string tName = Strings::Format("address_txs_%d", ymd);
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

void Parser::updateLastHeight(const int32_t newHeight) {
  string sql = Strings::Format("UPDATE `0_explorer_meta` SET `value` = %d,`updated_at`='%s' "
                               " WHERE `key`='jiexi.bootstrap.last_height'",
                               newHeight, date("%F %T").c_str());
  if (!dbExplorer_.execute(sql.c_str())) {
    THROW_EXCEPTION_DBEX("failed to update 'jiexi.bootstrap.last_height' to %d",
                         newHeight);
  }
  LOG_INFO("update 'jiexi.bootstrap.last_height' to %d", newHeight);
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

  // 构造插入SQL
  uint64_t difficulty = 0;
  BitsToDifficulty(header.nBits, difficulty);
  const int64_t rewardBlock = GetBlockValue(height, 0);
  const int64_t rewardFees  = blk.vtx[0].GetValueOut() - rewardBlock;
  assert(rewardFees >= 0);
  sql = Strings::Format("INSERT INTO `0_blocks` (`block_id`, `height`, `hash`,"
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
  db.updateOrThrowEx(sql, 1);

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

  // 从磁盘获取raw block
  _getRawBlockFromDisk(height, &blkRawHex, &chainId, &blockId);
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

  // 检测表 table.address_txs_xxxx 是否存在
  checkTableAddressTxs(blk.nTime);

  // 拿到 tx_hash -> tx_id 的对应关系
  vector<BlockTx *> blockTxs;
  std::map<uint256, bool> txUniq;
  std::map<uint256, int64_t> hash2id;
  for (auto & it : blk.vtx) {
    const uint256 hash = it.GetHash();
    int64_t txId;
    string hex;
    _getRawTxFromDisk(hash, height, &hex, &txId);
    hash2id[hash] = txId;
    txUniq[hash]  = false;

    auto ptr = new BlockTx(txId, height, blk.nTime, hash, it, (int32_t)hex.length()/2);
    blockTxs.push_back(ptr);
  }

  // 分析关联关系
  for (auto & it : blk.vtx) {
    for (auto & it2 : it.vin) {
      if (txUniq.find(it2.prevout.hash) != txUniq.end()) {
        // 将关联交易和自己都标记为true
        txUniq[it2.prevout.hash] = true;
        txUniq[it.GetHash()] = true;
      }
    }
  }

  // 插入数据至 table.0_blocks
  _insertBlock(dbExplorer_, blk, blockId, height, (int32_t)blkRawHex.length()/2);

  // 插入数据至 table.block_txs_xxxx
  _insertBlockTxs(dbExplorer_, blk, blockId, hash2id);

  // 分配交易
  size_t j = 0;
  const size_t mod = threadsNumber_ - 1;
  for (size_t i = 0; i < blk.vtx.size(); i++) {
    const uint256 hash = blk.vtx[i].GetHash();
    if (txUniq[hash]) {
      // 有关联关系的交易，放在最后一个槽里
      poolBlockTx_[threadsNumber_ - 1].push_back(blockTxs[i]);
    } else {
      // 没有关联关系的交易，放在前N-1个槽里
      poolBlockTx_[j++ % mod].push_back(blockTxs[i]);
    }
  }

  // 开启多线程处理
  for (int i = 0; i < threadsNumber_; i++) {
    boost::thread t3(boost::bind(&Parser::acceptTxThread, this, i));
  }
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
  vector<string> addressIdsStrVec = split(s, ',');
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

void _insertTxInputs(MySQLConnection &db, const CTransaction &tx,
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
    }
  } /* /for */

  // 执行插入 inputs
  if (!multiInsert(db, tableName, fields, values)) {
    THROW_EXCEPTION_DBEX("insert inputs fail, txId: %lld, hash: %s",
                         txId, tx.GetHash().ToString().c_str());
  }
}


void _insertTxOutputs(MySQLConnection &db, const CTransaction &tx,
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
                                         "%d,0,-1,'%s','%s'",
                                         // `tx_id`,`position`,`address`,`address_ids`
                                         txId, n, addressStr.c_str(), addressIdsStr.c_str(),
                                         // `value`,`output_script_asm`,`output_script_hex`,`output_script_type`
                                         out.nValue,
                                         outputScriptAsm.c_str(),
                                         outputScriptHex.length() < 32*1024*1024 ? outputScriptHex.c_str() : "",
                                         GetTxnOutputType(type) ? GetTxnOutputType(type) : "",
                                         // `is_spendable`,`spent_tx_id`,`spent_position`,`created_at`,`updated_at`
                                         (type != TX_NONSTANDARD && type != TX_NULL_DATA) ? 1 : 0,
                                         now.c_str(), now.c_str()));
  }

  // table.tx_outputs_xxxx
  const string tableNameTxOutputs = Strings::Format("tx_outputs_%04d", txId % 100);
  const string fieldsTxOutputs = "`tx_id`,`position`,`address`,`address_ids`,`value`,"
  "`output_script_asm`,`output_script_hex`,`output_script_type`,`is_spendable`,"
  "`spent_tx_id`,`spent_position`,`created_at`,`updated_at`";
  // multi insert outputs
  if (!multiInsert(db, tableNameTxOutputs, fieldsTxOutputs, itemValues)) {
    THROW_EXCEPTION_DBEX("insert outputs fail, txId: %lld, hash: %s",
                         txId, tx.GetHash().ToString().c_str());
  }
}


// 变更地址&地址对应交易记录
void _insertAddressTxs(MySQLConnection &db, const BlockTx &blkTx,
                       const map<int64_t, int64_t> &addressBalance) {
  MySQLResult res;
  char **row;
  string sql;

  // 前面步骤会确保该表已经存在
  const int32_t ymd = atoi(date("%Y%m%d", blkTx.nTime_).c_str());

  for (auto &it : addressBalance) {
    const int64_t addrID      = it.first;
    const int64_t balanceDiff = it.second;
    const string addrTableName = Strings::Format("addresses_%04d", addrID / BILLION);
    std::unordered_map<int64_t, LastestAddressInfo *>::iterator it2;

    // 获取地址信息
    int32_t beginTxYmd, prevTxYmd;
    int64_t beginTxID, prevTxId, totalRecv, finalBalance;

    // 锁定该地址，防止其他线程操作该地址
    while (!gAddrTxCache.lockAddr(addrID)) {
      usleep(500);
    }

    it2 = gAddrTxCache.find(addrID);
    if (it2 != gAddrTxCache.end()) {
      prevTxYmd    = it2->second->endTxYmd_;
      prevTxId     = it2->second->endTxId_;
      beginTxID    = it2->second->beginTxId_;
      beginTxYmd   = it2->second->beginTxYmd_;
      totalRecv    = it2->second->totalReceived_;
      finalBalance = it2->second->totalReceived_ - it2->second->totalSent_;
    } else {
      // 首次加载
      sql = Strings::Format("SELECT `end_tx_ymd`,`end_tx_id`,`total_received`,"
                            " `total_sent`,`begin_tx_id`,`begin_tx_ymd` "
                            " FROM `%s` WHERE `id`=%lld ",
                            addrTableName.c_str(), addrID);
      db.query(sql, res);
      assert(res.numRows() == 1);
      row = res.nextRow();

      prevTxYmd    = atoi(row[0]);
      prevTxId     = atoi64(row[1]);
      totalRecv    = atoi64(row[2]);
      finalBalance = totalRecv - atoi64(row[3]);
      beginTxID    = atoi64(row[4]);
      beginTxYmd   = atoi(row[5]);

      auto ptr = new LastestAddressInfo(beginTxYmd, prevTxYmd, beginTxID, prevTxId, atoi64(row[2]), atoi64(row[3]));
      gAddrTxCache.insert(addrID, ptr);
    }
    if ((it2 = gAddrTxCache.find(addrID)) == gAddrTxCache.end()) {
      THROW_EXCEPTION_DB("gAddrTxCache fatal");
    }

    // 处理前向记录
    if (prevTxYmd > 0 && prevTxId > 0) {
      // 更新前向记录
      sql = Strings::Format("UPDATE `address_txs_%d` SET `next_ymd`=%d, `next_tx_id`=%lld "
                            " WHERE `address_id`=%lld AND `tx_id`=%lld ",
                            prevTxYmd, ymd, blkTx.txId_, addrID, prevTxId);
      db.updateOrThrowEx(sql, 1);
    }
    assert(finalBalance + balanceDiff >= 0);

    // 插入当前记录
    sql = Strings::Format("INSERT INTO `address_txs_%d` (`address_id`, `tx_id`, `tx_height`,"
                          " `total_received`, `balance_diff`, `balance_final`, `prev_ymd`, "
                          " `prev_tx_id`, `next_ymd`, `next_tx_id`, `created_at`)"
                          " VALUES (%lld, %lld, %d, %lld, %lld, %lld,"
                          "         %d, %lld, 0, 0, '%s') ",
                          ymd, addrID, blkTx.txId_, blkTx.height_,
                          totalRecv + (balanceDiff > 0 ? balanceDiff : 0),
                          balanceDiff, finalBalance + balanceDiff,
                          prevTxYmd, prevTxId, date("%F %T").c_str());
    db.updateOrThrowEx(sql, 1);

    // 更新地址信息
    string sqlBegin = "";  // 是否更新 `begin_tx_ymd`/`begin_tx_id`
    if (beginTxYmd == 0) {
      assert(beginTxID  == 0);
      sqlBegin = Strings::Format("`begin_tx_ymd`=%d, `begin_tx_id`=%lld,",
                                 ymd, blkTx.txId_);
      it2->second->beginTxId_  = blkTx.txId_;
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
                          ymd, blkTx.txId_, sqlBegin.c_str(),
                          date("%F %T").c_str(), addrID);
    db.updateOrThrowEx(sql, 1);

    it2->second->totalReceived_ += (balanceDiff > 0 ? balanceDiff : 0);
    it2->second->totalSent_     += (balanceDiff < 0 ? balanceDiff * -1 : 0);
    it2->second->endTxId_  = blkTx.txId_;
    it2->second->endTxYmd_ = ymd;

    // 对该地址解锁
    gAddrTxCache.unlockAddr(addrID);
  } /* /for */
}

// 插入交易
void _insertTx(MySQLConnection &db, const BlockTx &blkTx, int64_t valueIn) {
  // (`tx_id`, `hash`, `height`, `is_coinbase`, `version`, `lock_time`, `size`, `fee`, `total_in_value`, `total_out_value`, `inputs_count`, `outputs_count`, `created_at`)
  const string tName = Strings::Format("txs_%04d", blkTx.txId_ / BILLION);
  const CTransaction &tx = blkTx.tx_;  // alias
  string sql;

  int64_t fee = 0;
  const int64_t valueOut = tx.GetValueOut();
  if (tx.IsCoinBase()) {
    // coinbase的fee为 block rewards
    fee = valueOut - GetBlockValue(blkTx.height_, 0);
  } else {
    fee = valueIn - valueOut;
  }

  sql = Strings::Format("INSERT INTO `%s`(`tx_id`, `hash`, `height`, `block_timestamp`,`is_coinbase`,"
                        " `version`, `lock_time`, `size`, `fee`, `total_in_value`, "
                        " `total_out_value`, `inputs_count`, `outputs_count`, `created_at`)"
                        " VALUES (%lld, '%s', %d, %u, %d, %d, %u, %d, %lld, %lld, %lld, %d, %d, '%s') ",
                        tName.c_str(),
                        // `tx_id`, `hash`, `height`, `block_timestamp`
                        blkTx.txId_, blkTx.txHash_.ToString().c_str(), blkTx.height_, blkTx.nTime_,
                        // `is_coinbase`, `version`, `lock_time`
                        tx.IsCoinBase() ? 1 : 0, tx.nVersion, tx.nLockTime,
                        // `size`, `fee`, `total_in_value`, `total_out_value`
                        blkTx.txSize_, fee, valueIn, valueOut,
                        // `inputs_count`, `outputs_count`, `created_at`
                        tx.vin.size(), tx.vout.size(), date("%F %T").c_str());
  db.updateOrThrowEx(sql, 1);
}

void Parser::acceptTxThread(const int32_t i) {
  vector<BlockTx *> &blkTxs = poolBlockTx_[i];  // alias

  while (blkTxs.size() > 0) {
    acceptTx(*poolDB_[i], blkTxs[blkTxs.size() - 1]);
    blkTxs.pop_back();
  }
}

// 接收一个新的交易
void Parser::acceptTx(MySQLConnection &db,  const BlockTx *blkTx) {

  // 硬编码特殊交易处理
  //
  // 1. tx hash: d5d27987d2a3dfc724e359870c6644b40e497bdc0589a033220fe15429d88599
  // 该交易在两个不同的高度块(91812, 91842)中出现过
  // 91842块中有且仅有这一个交易
  //
  // 2. tx hash: e3bf3d07d4b0375638d5f1db5255fe07ba2c4cb067cd81b84ee974b6585fb468
  // 该交易在两个不同的高度块(91722, 91880)中出现过
  if ((blkTx->height_ == 91842 &&
       blkTx->txHash_ == uint256("d5d27987d2a3dfc724e359870c6644b40e497bdc0589a033220fe15429d88599")) ||
      (blkTx->height_ == 91880 &&
       blkTx->txHash_ == uint256("e3bf3d07d4b0375638d5f1db5255fe07ba2c4cb067cd81b84ee974b6585fb468"))) {
    LOG_WARN("ignore tx, height: %d, hash: %s",
             blkTx->height_, blkTx->txHash_.ToString().c_str());
    return;
  }

  // 交易的输入之和，遍历交易后才能得出
  int64_t valueIn = 0;

  // 同一个地址只能产生一条交易记录: address <-> tx <-> balance_diff
  map<int64_t, int64_t> addressBalance;

  // 处理inputs
  _insertTxInputs(db, blkTx->tx_, blkTx->txId_, addressBalance, valueIn);

  // 处理outputs
  _insertTxOutputs(db, blkTx->tx_, blkTx->txId_, blkTx->height_, addressBalance);

  // 变更地址相关信息
  _insertAddressTxs(db, *blkTx, addressBalance);

  // 插入交易tx
  _insertTx(db, *blkTx, valueIn);
}


// 获取上次处理高度
int32_t Parser::getLastHeight() {
  MySQLResult res;
  int32_t lastHeight = -1;
  char **row = nullptr;
  string sql;

  // find last tx log ID
  sql = "SELECT `value` FROM `0_explorer_meta` WHERE `key`='jiexi.bootstrap.last_height'";
  dbExplorer_.query(sql, res);
  if (res.numRows() == 1) {
    row = res.nextRow();
    lastHeight = atoi(row[0]);
    assert(lastHeight >= 0);
  } else {
    const string now = date("%F %T");
    sql = Strings::Format("INSERT INTO `0_explorer_meta`(`key`,`value`,`created_at`,`updated_at`) "
                          " VALUES('jiexi.bootstrap.last_height', '-1', '%s', '%s')",
                          now.c_str(), now.c_str());
    dbExplorer_.update(sql);
    lastHeight = -1;  // default value is zero
  }
  return lastHeight;
}

// 从磁盘读取 raw_tx 批量文件
void _loadRawTxsFromDisk(map<uint256, std::pair<string, int64_t> > &txCache, const int32_t height) {
  string dir = Config::GConfig.get("cache.rawdata.dir", "");
  // 尾部添加 '/'
  if (dir.length() == 0) {
    dir = "./";
  }
  else if (dir[dir.length()-1] != '/') {
    dir += "/";
  }

  // 遍历循环64个文件
  const int32_t height2 = (height / 10000) * 10000;
  string path = Strings::Format("%d_%d", height2, height2 + 9999);
  for (int i = 0; i < 64; i++) {
    const string fname = Strings::Format("%s%s/raw_txs_%04d",
                                         dir.c_str(), path.c_str(), i);
    LOG_INFO("load raw tx file: %s", fname.c_str());
    std::ifstream input(fname);
    std::string line;
    while (std::getline(input, line)) {
      auto arr = split(line, ',');
      // line: raw_id,hash,hex,create_time
      const uint256 hash(arr[1]);
      const int64_t txId = atoi64(arr[0].c_str());
      txCache[hash] = std::make_pair(arr[2], txId);
    }
  }
}

void _getRawTxFromDisk(const uint256 &hash, const int32_t height,
                       string *hex, int64_t *txId) {
  // 从磁盘直接读取文件，缓存起来，减少数据库交互
  static map<uint256, std::pair<string, int64_t> > txCache;
  static int32_t heightBegin = -1;

  if (heightBegin != (height / 10000)) {
    // 载入数据
    LOG_INFO("try load raw tx data from disk...");
    txCache.clear();
    heightBegin = height / 10000;
    _loadRawTxsFromDisk(txCache, height);
  }

  auto it = txCache.find(hash);
  if (it == txCache.end()) {
    THROW_EXCEPTION_DBEX("can't find rawtx from disk cache, txLogId: %lld");
  }
  if (hex != nullptr) {
    *hex  = it->second.first;
  }
  if (txId) {
    *txId = it->second.second;
  }
}
