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

/////////////////////////////////  RawBlock  ///////////////////////////////////

RawBlock::RawBlock(const int64_t blockId, const int32_t height, const int32_t chainId,
                   const uint256 hash, const string &hex) {
  blockId_ = blockId;
  height_  = height;
  chainId_ = chainId;
  hash_    = hash;
  hex_     = hex;
}

////////////////////////////  AddressInfo  //////////////////////////////

AddressInfo::AddressInfo(const int64_t lastUseIdx) {
  txCount_  = 0;
  received_ = 0;
  sent_     = 0;

  unconfirmedTxCount_  = 0;
  unconfirmedReceived_ = 0;
  unconfirmedSent_     = 0;

  unspentTxCount_ = 0;
  unspentTxIndex_ = -1;
  lastConfirmedTxIdx_  = -1;

  lastUseIdx_ = lastUseIdx;
}
AddressInfo::AddressInfo(const int32_t txCount,
                                       const int64_t received, const int64_t sent,
                                       const int32_t unconfirmedTxCount,
                                       const int64_t unconfirmedReceived,
                                       const int64_t unconfirmedSent,
                                       const int32_t unspentTxCount, const int32_t unspentTxIndex,
                                       const int32_t lastConfirmedTxIdx,
                                       const int64_t lastUseIdx) {
  txCount_  = txCount;
  received_ = received;
  sent_     = sent;

  unconfirmedTxCount_  = unconfirmedTxCount;
  unconfirmedReceived_ = unconfirmedReceived;
  unconfirmedSent_     = unconfirmedSent;

  unspentTxCount_ = unspentTxCount;
  unspentTxIndex_ = unspentTxIndex;

  lastConfirmedTxIdx_  = lastConfirmedTxIdx;

  lastUseIdx_ = lastUseIdx;
}

AddressInfo::AddressInfo(const AddressInfo &a) {
  txCount_  = a.txCount_;
  received_ = a.received_;
  sent_     = a.sent_;

  unconfirmedTxCount_  = a.unconfirmedTxCount_;
  unconfirmedReceived_ = a.unconfirmedReceived_;
  unconfirmedSent_     = a.unconfirmedSent_;

  unspentTxCount_ = a.unspentTxCount_;
  unspentTxIndex_ = a.unspentTxIndex_;

  lastConfirmedTxIdx_  = a.lastConfirmedTxIdx_;

  lastUseIdx_ = a.lastUseIdx_;
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
running_(true),
unconfirmedTxsSize_(0), unconfirmedTxsCount_(0),
kvdb_(Config::GConfig.get("rocksdb.path", "./rocksdb")),
blkTs_(2016), notifyProducer_(nullptr)
{
  notifyFileLog2Producer_ = Config::GConfig.get("notify.log2producer.file");
  if (notifyFileLog2Producer_.empty()) {
    THROW_EXCEPTION_DBEX("empty config: notify.log2producer.file");
  }

  //
  // notification.dir
  //
  {
    string dir = Config::GConfig.get("notification.dir", "");
    if (*(std::prev(dir.end())) == '/') {  // remove last '/'
      dir.resize(dir.length() - 1);
    }
    notifyProducer_ = new NotifyProducer(dir);
    notifyProducer_->init();
  }
}

Parser::~Parser() {
  stop();

  if (watchNotifyThread_.joinable()) {
    watchNotifyThread_.join();
  }

  if (notifyProducer_ != nullptr) {
    delete notifyProducer_;
    notifyProducer_ = nullptr;
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
  if (!dbExplorer_.ping()) {
    LOG_FATAL("connect to explorer DB failure");
    return false;
  }

  // 检测DB参数： max_allowed_packet
  const int32_t maxAllowed = atoi(dbExplorer_.getVariable("max_allowed_packet").c_str());
  const int32_t kMinAllowed = 8*1024*1024;
  if (maxAllowed < kMinAllowed) {
    LOG_FATAL("mysql.db.max_allowed_packet(%d) is too small, should >= %d",
              maxAllowed, kMinAllowed);
    return false;
  }

  //
  // 90_tparser_unconfirmed_txs_count, 90_tparser_unconfirmed_txs_size
  //
  {
    string key;
    string value;

    // unconfirmedTxsCount_
    key = "90_tparser_unconfirmed_txs_count";
    kvdb_.get(key, value);
    if (value.size() != 0) {
      unconfirmedTxsCount_ = atoi(value.c_str());
    }

    key = "90_tparser_unconfirmed_txs_size";
    kvdb_.get(key, value);
    if (value.size() != 0) {
      unconfirmedTxsSize_ = atoi64(value.c_str());
    }
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

void Parser::updateLastTxlog2Id(const int64_t newId) {
  string sql = Strings::Format("UPDATE `0_explorer_meta` SET `value` = '%lld',`updated_at`='%s' "
                               " WHERE `key`='jiexi.last_txlog2_offset'",
                               newId, date("%F %T").c_str());
  dbExplorer_.updateOrThrowEx(sql, 1);
}

void _insertBlock(KVDB &kvdb, const CBlock &blk, const int32_t height, const int32_t blockSize) {
  CBlockHeader header = blk.GetBlockHeader();  // alias
  const uint256 blockHash    = blk.GetHash();
  const string blockHashStr  = blockHash.ToString();
  const string prevBlockHash = header.hashPrevBlock.ToString();
  flatbuffers::FlatBufferBuilder fbb;

  double difficulty = 0;
  BitsToDifficulty(header.nBits, difficulty);
  const uint64_t pdiff = TargetToPdiff(blockHash);

  const int64_t rewardBlock = GetBlockValue(height, 0);
  const int64_t rewardFees  = blk.vtx[0].GetValueOut() - rewardBlock;
  assert(rewardFees >= 0);

  {
    auto fb_createdAt = fbb.CreateString(date("%F %T"));
    auto fb_mrklRoot = fbb.CreateString(header.hashMerkleRoot.ToString());
    // next hash需要"填零"，保持长度一致, 当下一个块来临时，直接对那块内存进行改写操作
    auto fb_nextBlkHash = fbb.CreateString(uint256().ToString());
    auto fb_prevBlkHash = fbb.CreateString(prevBlockHash);

    fbe::BlockBuilder blockBuilder(fbb);
    blockBuilder.add_bits(header.nBits);
    blockBuilder.add_created_at(fb_createdAt);
    blockBuilder.add_difficulty(difficulty);
    blockBuilder.add_mrkl_root(fb_mrklRoot);
    blockBuilder.add_next_block_hash(fb_nextBlkHash);
    blockBuilder.add_nonce(header.nNonce);
    blockBuilder.add_pool_difficulty(pdiff);
    blockBuilder.add_prev_block_hash(fb_prevBlkHash);
    blockBuilder.add_reward_block(rewardBlock);
    blockBuilder.add_reward_fees(rewardFees);
    blockBuilder.add_size(blockSize);
    blockBuilder.add_timestamp(header.nTime);
    blockBuilder.add_tx_count((uint32_t)blk.vtx.size());
    blockBuilder.add_version(header.nVersion);
    blockBuilder.Finish();

    // 11_{block_hash}, 需紧接 blockBuilder.Finish()
    const string key11 = Strings::Format("%s%s", KVDB_PREFIX_BLOCK_OBJECT, blockHash.ToString().c_str());
    kvdb.set(key11, fbb.GetBufferPointer(), fbb.GetSize());
  }

  // 10_{block_height}
  const string key10 = Strings::Format("%s%010d", KVDB_PREFIX_BLOCK_HEIGHT, height);
  kvdb.set(key10, blockHashStr);

  // 更新前向块信息, 高度一以后的块需要，创始块不需要
  if (height > 0) {
    // 11_{block_hash}
    const string prevBlkKey = Strings::Format("%s%s", KVDB_PREFIX_BLOCK_OBJECT, prevBlockHash.c_str());
    string value;
    kvdb.get(prevBlkKey, value);

    auto prevBlock = flatbuffers::GetMutableRoot<fbe::Block>((void *)value.data());
    for (flatbuffers::uoffset_t i = 0; i < blockHashStr.length(); i++) {
      prevBlock->mutable_next_block_hash()->Mutate(i, blockHashStr[i]);
    }
    kvdb.set(prevBlkKey, value);
  }
}

// 已经存在则会忽略
void _insertBlockTxs(KVDB &kvdb, const CBlock &blk) {
  const int32_t kBatchSize = 500;  // 每500条为一个批次

  int32_t i = 0;
  string key;
  string value;
  for (const auto &tx : blk.vtx) {
    value += tx.GetHash().ToString();

    if ((i+1) % kBatchSize == 0) {
      key = Strings::Format("%s%s_%d", KVDB_PREFIX_BLOCK_TXS_STR,
                            blk.GetHash().ToString().c_str(), (int32_t)(i/kBatchSize));
      kvdb.set(key, value);
      value.clear();
    }
    i++;
  }

  if (value.size() != 0) {
    key = Strings::Format("%s%s_%d", KVDB_PREFIX_BLOCK_TXS_STR,
                          blk.GetHash().ToString().c_str(), (int32_t)(i/kBatchSize));
    kvdb.set(key, value);
    value.clear();
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

  // 插入数据至 table.0_blocks
  _insertBlock(kvdb_, blk, txLog2->blkHeight_, (int32_t)blkRawHex.length()/2);

  // 插入区块交易
  _insertBlockTxs(kvdb_, blk);
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

// 获取地址信息
static AddressInfo *_getAddressInfo(KVDB &kvdb, const string &address) {
  static std::map<string, AddressInfo *> _addrTxCache;
  static int64_t lastUseIdx = 0;

  // clear, 数量限制，防止长时间运行后占用过多内存
  const int64_t kMaxCacheCount  = 500*10000;

  std::map<string, AddressInfo *>::iterator it;

  it = _addrTxCache.find(address);
  if (it == _addrTxCache.end()) {
    const string key = Strings::Format("%s%s", KVDB_PREFIX_ADDR_OBJECT, address.c_str());
    string value;
    AddressInfo *addressPtr;

    kvdb.get(key, value);
    if (value.size() == 0) {
      // 未找到则新建一个
      addressPtr = new AddressInfo(lastUseIdx++);
    } else {
      auto fbAddress = flatbuffers::GetRoot<fbe::Address>(value.data());
      addressPtr = new AddressInfo(fbAddress->tx_count(),
                                          fbAddress->received(),
                                          fbAddress->sent(),
                                          fbAddress->unconfirmed_tx_count(),
                                          fbAddress->unconfirmed_received(),
                                          fbAddress->unconfirmed_sent(),
                                          fbAddress->unspent_tx_count(),
                                          fbAddress->unspent_tx_index(),
                                          fbAddress->last_confirmed_tx_idx(),
                                          lastUseIdx++);
    }
    _addrTxCache[address] = addressPtr;

    if (_addrTxCache.size() > kMaxCacheCount) {
      LOG_INFO("clear _addrTxCache begin, count: %llu", _addrTxCache.size());
      for (auto it2 = _addrTxCache.begin(); it2 != _addrTxCache.end(); ) {
        // 每次删除一半
        if (it2->second->lastUseIdx_ > lastUseIdx - kMaxCacheCount / 2) {
          it2++;
          continue;
        }
        // 删除超出超过 kExpiredDays 天的记录
        it2 = _addrTxCache.erase(it2);
      }
      LOG_INFO("clear _addrTxCache end, count: %llu", _addrTxCache.size());
    }
  }
  it = _addrTxCache.find(address);
  assert(it != _addrTxCache.end());

  return it->second;
}

// 变更地址&地址对应交易记录
static
void _accpetTx_insertAddressTxs(KVDB &kvdb, class TxLog2 *txLog2,
                                const map<string, int64_t> &addressBalance) {
  //
  // accept: 加入交易链尾，不涉及调整交易链
  //
  flatbuffers::FlatBufferBuilder fbb;
  auto txhash = fbb.CreateString(txLog2->txHash_.ToString());

  for (auto &it : addressBalance) {
    // alias
    const string &address     = it.first;
    const int64_t balanceDiff = it.second;

    // 获取地址信息
    AddressInfo *addr = _getAddressInfo(kvdb, address);
    const int32_t addressTxIndex = addr->txCount_;  // index start from 0

    //
    // AddressTx
    //
    {
      fbe::AddressTxBuilder addressTxBuilder(fbb);
      addressTxBuilder.add_balance_diff(balanceDiff);
      addressTxBuilder.add_tx_hash(txhash);
      addressTxBuilder.add_tx_height(-1);
      addressTxBuilder.add_ymd(-1);
      addressTxBuilder.Finish();

      // 21_{address}_{010index}
      const string key21 = Strings::Format("%s%s_%010d", KVDB_PREFIX_ADDR_TX,
                                           address.c_str(), addressTxIndex);
      kvdb.set(key21, fbb.GetBufferPointer(), fbb.GetSize());
    }

    //
    // AddressTxIdx
    //
    {
      // 22_{address}_{tx_hash}
      const string key22 = Strings::Format("%s%s_%s", KVDB_PREFIX_ADDR_TX_INDEX, address.c_str(),
                                           txLog2->txHash_.ToString().c_str());
      kvdb.set(key22, Strings::Format("%d", addressTxIndex));
    }

    //
    // 更新地址信息
    //
    const int64_t balanceReceived = (balanceDiff > 0 ? balanceDiff : 0);
    const int64_t balanceSent     = (balanceDiff < 0 ? balanceDiff * -1 : 0);

    addr->received_ += balanceReceived;
    addr->sent_     += balanceSent;
    addr->unconfirmedReceived_ += balanceReceived;
    addr->unconfirmedSent_     += balanceSent;
    addr->unconfirmedTxCount_++;
    addr->txCount_++;

  } /* /for */
}

void Parser::addUnconfirmedTxPool(class TxLog2 *txLog2) {
  unconfirmedTxsCount_++;
  unconfirmedTxsSize_ += txLog2->txHex_.length() / 2;

  // unconfirmedTxsCount_
  {
    const string key = "90_tparser_unconfirmed_txs_count";
    const string value = Strings::Format("%d", unconfirmedTxsCount_);
    kvdb_.set(key, (const uint8_t *)value.data(), value.size());
  }

  // unconfirmedTxsSize_
  {
    const string key = "90_tparser_unconfirmed_txs_size";
    const string value = Strings::Format("%lld", unconfirmedTxsSize_);
    kvdb_.set(key, (const uint8_t *)value.data(), value.size());
  }
}

void Parser::removeUnconfirmedTxPool(class TxLog2 *txLog2) {
  unconfirmedTxsCount_--;
  unconfirmedTxsSize_ -= txLog2->txHex_.length() / 2;

  // unconfirmedTxsCount_
  {
    const string key = "90_tparser_unconfirmed_txs_count";
    const string value = Strings::Format("%d", unconfirmedTxsCount_);
    kvdb_.set(key, (const uint8_t *)value.data(), value.size());
  }

  // unconfirmedTxsSize_
  {
    const string key = "90_tparser_unconfirmed_txs_size";
    const string value = Strings::Format("%lld", unconfirmedTxsSize_);
    kvdb_.set(key, (const uint8_t *)value.data(), value.size());
  }
}

// 写入通知日志文件
void Parser::writeNotificationLogs(const map<int64_t, int64_t> &addressBalance,
                                   class TxLog2 *txLog2) {
//  static string buffer;
//  if (notifyProducer_ == nullptr) {
//    return;
//  }
//
//  buffer.clear();
//  for (auto it : addressBalance) {
//    const int64_t addrID      = it.first;
//    const int64_t balanceDiff = it.second;
//    AddressInfo *addr = _getAddressInfo(dbExplorer_, addrID);
//
//    NotifyItem item(txLog2->type_, txLog2->tx_.IsCoinBase(),
//                    addr->addrId_, txLog2->txId_, addr->addressStr_,
//                    txLog2->txHash_, balanceDiff,
//                    txLog2->blkHeight_, txLog2->blkId_,
//                    (txLog2->blkId_ > 0 ? blockId2Hash(txLog2->blkId_) : uint256()));
//    buffer.append(item.toStrLineWithTime() + "\n");
//  }
//  notifyProducer_->write(buffer);
}

// 插入交易的raw hex
static void _acceptTx_insertRawHex(KVDB &kvdb, const string &txHex, const uint256 &hash) {
  const string key = KVDB_PREFIX_TX_RAW_HEX + hash.ToString();
  if (kvdb.keyExist(key)) {
    return;  // already exist
  }

  vector<char> buff;
  Hex2Bin(txHex.c_str(), txHex.length(), buff);
  kvdb.set(key, (uint8_t *)buff.data(), buff.size());
}

// 插入交易object对象
static void _acceptTx_insertTxObject(KVDB &kvdb, const uint256 &hash,
                                     flatbuffers::FlatBufferBuilder *fbb) {
  const string key = KVDB_PREFIX_TX_OBJECT + hash.ToString();
  if (kvdb.keyExist(key)) {
    return;  // already exist
  }

  kvdb.set(key, fbb->GetBufferPointer(), fbb->GetSize());
}

// 移除prev unspent outputs
static void _acceptTx_removeUnspentOutputs(KVDB &kvdb, const uint256 &hash, CTransaction &tx,
                                           vector<const fbe::TxOutput *> &prevTxOutputs) {
  assert(prevTxOutputs.size() == tx.vin.size());

  string key;
  int n = -1;  // postion

  for (auto fb_txoutput : prevTxOutputs) {
    n++;

    auto addresses = fb_txoutput->addresses();
    const uint256 &prevHash = tx.vin[n].prevout.hash;

    // 绝大部分只有一个地址，但存在多个地址可能，所以不得不采用循环
    for (flatbuffers::uoffset_t j = 0; j < addresses->size(); j++) {
      const string address = addresses->operator[](j)->str();

      AddressInfo *addr = _getAddressInfo(kvdb, address);
      addr->unspentTxCount_--;  // 地址未花费数量减一

      // 某个地址的未花费index
      // 24_{address}_{tx_hash}_{position}
      key = Strings::Format("%s%s_%s_%d", KVDB_PREFIX_ADDR_UNSPENT_INDEX,
                            address.c_str(), prevHash.ToString().c_str(), (int32_t)n);
      string value;
      kvdb.get(key, value);
      auto unspentOutputIdx = flatbuffers::GetRoot<fbe::AddressUnspentIdx>(value.data());

      // 删除之
      // 23_{address}_{010index}
      key = Strings::Format("%s%s_%010d", KVDB_PREFIX_ADDR_UNSPENT, address.c_str(),
                            unspentOutputIdx->index());
      kvdb.del(key);
    }
  }
}

// 生成被消费记录
static void _acceptTx_insertSpendTxs(KVDB &kvdb, const uint256 &hash, CTransaction &tx) {
  flatbuffers::FlatBufferBuilder fbb;
  string key;
  auto fb_spentHash = fbb.CreateString(hash.ToString());

  int n = -1;  // postion
  for (const auto &in : tx.vin) {
    n++;
    const uint256 &prevHash = in.prevout.hash;
    // 02_{tx_hash}_{position}
    key = Strings::Format("%s%s_%d", KVDB_PREFIX_TX_SPEND,
                          prevHash.ToString().c_str(), (int32_t)in.prevout.n);

    fbe::TxSpentByBuilder txSpentByBuilder(fbb);
    txSpentByBuilder.add_position(n);
    txSpentByBuilder.add_tx_hash(fb_spentHash);
    txSpentByBuilder.Finish();
    kvdb.set(key, fbb.GetBufferPointer(), fbb.GetSize());
  }
}

// 插入地址的 unspent 记录
static void _acceptTx_insertAddressUnspent(KVDB &kvdb, const int64_t nValue,
                                           const string &address, const uint256 &hash,
                                           const int32_t position, const int32_t position2) {
  flatbuffers::FlatBufferBuilder fbb;
  auto fb_spentHash = fbb.CreateString(hash.ToString());
  AddressInfo *addr = _getAddressInfo(kvdb, address);

  addr->unspentTxIndex_++;
  addr->unspentTxCount_++;

  const int32_t addressUnspentIndex = addr->unspentTxIndex_;

  //
  // 24_{address}_{tx_hash}_{position}
  //
  {
    const string key24 = Strings::Format("%s%s_%s_%d", KVDB_PREFIX_ADDR_UNSPENT_INDEX,
                                         address.c_str(), hash.ToString().c_str(), position);
    fbe::AddressUnspentIdxBuilder addressUnspentIdxBuilder(fbb);
    addressUnspentIdxBuilder.add_index(addressUnspentIndex);
    addressUnspentIdxBuilder.Finish();
    kvdb.set(key24, fbb.GetBufferPointer(), fbb.GetSize());
  }

  //
  // 23_{address}_{010index}
  //
  {
    const string key23 = Strings::Format("%s%s_%010d", KVDB_PREFIX_ADDR_UNSPENT,
                                         address.c_str(), addressUnspentIndex);
    fbe::AddressUnspentBuilder addressUnspentBuilder(fbb);
    addressUnspentBuilder.add_position(position);
    addressUnspentBuilder.add_position2(position2);
    addressUnspentBuilder.add_tx_hash(fb_spentHash);
    addressUnspentBuilder.add_value(nValue);
    addressUnspentBuilder.Finish();
    kvdb.set(key23, fbb.GetBufferPointer(), fbb.GetSize());
  }
}

map<string, int64_t> Parser::getTxAddressBalance(const CTransaction &tx) {
  map<string, int64_t> addressBalance;

  //
  // inputs
  //
  if (!tx.IsCoinBase()) {
    vector<const fbe::TxOutput *> prevTxOutputs;
    vector<string> prevTxsData;  // prevTxsData 必须一直存在，否则 prevTxOutputs 读取无效内存

    // 读取前向交易的输出
    kvdb_.getPrevTxOutputs(tx, prevTxsData, prevTxOutputs);

    for (size_t n = 0; n < tx.vin.size(); n ++) {
      auto addresses      = prevTxOutputs[n]->addresses();
      const int64_t value = prevTxOutputs[n]->value();

      for (flatbuffers::uoffset_t j = 0; j < addresses->size(); j++) {
        const string address = addresses->operator[](j)->str();
        addressBalance[address] += value * -1;
      }
    }
  }

  //
  // outputs
  //
  {
    for (const auto &out : tx.vout) {
      string addressStr;
      txnouttype type;
      vector<CTxDestination> addresses;
      int nRequired;
      if (!ExtractDestinations(out.scriptPubKey, type, addresses, nRequired)) {
        continue;
      }
      for (auto &addr : addresses) {
        addressBalance[CBitcoinAddress(addr).ToString()] += out.nValue;
      }
    }
  }
  return addressBalance;
}

// 接收一个新的交易
void Parser::acceptTx(class TxLog2 *txLog2) {
  assert(txLog2->blkHeight_ == -1);
  CTransaction &tx = txLog2->tx_;

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

  int64_t valueIn = 0;  // 交易的输入之和，遍历交易后才能得出
  vector<const fbe::TxOutput *> prevTxOutputs;
  vector<string> prevTxsData;  // prevTxsData 必须一直存在，否则 prevTxOutputs 读取无效内存
  map<string/* address */, int64_t/* balance_diff */> addressBalance;

  flatbuffers::FlatBufferBuilder fbb;
  vector<flatbuffers::Offset<fbe::TxInput > > fb_txInputs;
  vector<flatbuffers::Offset<fbe::TxOutput> > fb_txOutputs;

  // 读取前向交易的输出
  kvdb_.getPrevTxOutputs(tx, prevTxsData, prevTxOutputs);

  //
  // inputs
  //
  {
    int n = -1;
    for (const auto &in : tx.vin) {
      n++;
      auto fb_ScriptHex = fbb.CreateString(HexStr(in.scriptSig.begin(), in.scriptSig.end()));

      if (txLog2->tx_.IsCoinBase()) {
        // 插入当前交易的inputs, coinbase tx的 scriptSig 不做decode，可能含有非法字符
        // 通常无法解析成功, 不解析 scriptAsm
        // coinbase无法担心其长度，bitcoind对coinbase tx的coinbase字段长度做了限制

        fbe::TxInputBuilder txInputBuilder(fbb);
        txInputBuilder.add_prev_position(-1);
        txInputBuilder.add_prev_value(0);
        txInputBuilder.add_script_hex(fb_ScriptHex);
        txInputBuilder.add_sequence(in.nSequence);
        fb_txInputs.push_back(txInputBuilder.Finish());
      }
      else
      {
        auto addresses      = prevTxOutputs[n]->addresses();
        const int64_t value = prevTxOutputs[n]->value();

        vector<flatbuffers::Offset<flatbuffers::String> > fb_addressesVec;
        for (flatbuffers::uoffset_t j = 0; j < addresses->size(); j++) {
          const string address = addresses->operator[](j)->str();
          addressBalance[address] += value * -1;
          fb_addressesVec.push_back(fbb.CreateString(address));
        }
        valueIn += value;

        {
          auto fb_prevAddresses = fbb.CreateVector(fb_addressesVec);
          auto fb_prevTxHash    = fbb.CreateString(in.prevout.hash.ToString());
          auto fb_ScriptAsm     = fbb.CreateString(in.scriptSig.ToString());

          fbe::TxInputBuilder txInputBuilder(fbb);
          txInputBuilder.add_prev_addresses(fb_prevAddresses);
          txInputBuilder.add_prev_position(in.prevout.n);
          txInputBuilder.add_prev_tx_hash(fb_prevTxHash);
          txInputBuilder.add_prev_value(value);
          txInputBuilder.add_script_asm(fb_ScriptAsm);
          txInputBuilder.add_script_hex(fb_ScriptHex);
          txInputBuilder.add_sequence(in.nSequence);
          fb_txInputs.push_back(txInputBuilder.Finish());
        }
      }
    }
  }
  auto fb_txObjInputs = fbb.CreateVector(fb_txInputs);

  //
  // outputs
  //
  {
    int n = -1;
    for (const auto &out : tx.vout) {
      n++;
      string addressStr;
      txnouttype type;
      vector<CTxDestination> addresses;
      int nRequired;
      ExtractDestinations(out.scriptPubKey, type, addresses, nRequired);

      // 解地址可能失败，但依然有 tx_outputs_xxxx 记录
      // 输出无有效地址的奇葩TX:
      //   testnet3: e920604f540fec21f66b6a94d59ca8b1fbde27fc7b4bc8163b3ede1a1f90c245

      // multiSig 可能由多个输出地址: https://en.bitcoin.it/wiki/BIP_0011
      vector<flatbuffers::Offset<flatbuffers::String> > fb_addressesVec;
      int16_t j = -1;
      for (auto &addr : addresses) {
        j++;
        const string addrStr = CBitcoinAddress(addr).ToString();
        addressBalance[addrStr] += out.nValue;
        fb_addressesVec.push_back(fbb.CreateString(addrStr));

        // address unspent
        _acceptTx_insertAddressUnspent(kvdb_, out.nValue,
                                       addrStr, txLog2->txHash_, n, j);
      }

      // output Hex奇葩的交易：
      // http://tbtc.blockr.io/tx/info/c333a53f0174166236e341af9cad795d21578fb87ad7a1b6d2cf8aa9c722083c
      string outputScriptAsm = out.scriptPubKey.ToString();
      const string outputScriptHex = HexStr(out.scriptPubKey.begin(), out.scriptPubKey.end());
      if (outputScriptAsm.length() > 2*1024*1024) {
        outputScriptAsm = "";
      }

      auto fb_addresses  = fbb.CreateVector(fb_addressesVec);
      auto fb_scriptAsm  = fbb.CreateString(outputScriptAsm);
      auto fb_scriptHex  = fbb.CreateString(outputScriptHex);
      auto fb_scriptType = fbb.CreateString(GetTxnOutputType(type) ? GetTxnOutputType(type) : "");

      fbe::TxOutputBuilder txOutputBuilder(fbb);
      txOutputBuilder.add_addresses(fb_addresses);
      txOutputBuilder.add_value(out.nValue);
      txOutputBuilder.add_script_asm(fb_scriptAsm);
      txOutputBuilder.add_script_hex(fb_scriptHex);
      txOutputBuilder.add_script_type(fb_scriptType);
      fb_txOutputs.push_back(txOutputBuilder.Finish());
    }
  }
  auto fb_txObjOutputs = fbb.CreateVector(fb_txOutputs);

  const int64_t valueOut = txLog2->tx_.GetValueOut();
  int64_t fee = 0;
  {
    if (txLog2->tx_.IsCoinBase()) {
      fee = valueOut;  // coinbase的fee为 block rewards
    } else {
      fee = valueIn - valueOut;
    }
  }

  //
  // build tx object
  //
  auto createdAt = fbb.CreateString(date("%F %T"));

  fbe::TxBuilder txBuilder(fbb);
  txBuilder.add_is_coinbase(tx.IsCoinBase());
  txBuilder.add_version(tx.nVersion);
  txBuilder.add_lock_time(tx.nLockTime);
  txBuilder.add_size((int)(txLog2->txHex_.length()/2));
  txBuilder.add_fee(fee);
  txBuilder.add_inputs(fb_txObjInputs);
  txBuilder.add_inputs_count((int)tx.vin.size());
  txBuilder.add_inputs_value(valueIn);
  txBuilder.add_outputs(fb_txObjOutputs);
  txBuilder.add_outputs_count((int)tx.vout.size());
  txBuilder.add_outputs_value(valueOut);
  txBuilder.add_created_at(createdAt);
  txBuilder.Finish();
  // insert tx object, 需要紧跟 txBuilder.Finish() 函数，否则 fbb 内存会破坏
  _acceptTx_insertTxObject(kvdb_, txLog2->txHash_, &fbb);

  // insert tx raw hex，实际插入binary data，减小一半体积
  _acceptTx_insertRawHex(kvdb_, txLog2->txHex_, txLog2->txHash_);

  // remove unspent，移除未花费交易，扣减计数器
  _acceptTx_removeUnspentOutputs(kvdb_, txLog2->txHash_, tx, prevTxOutputs);

  // insert spent txs, 插入前向交易的花费记录
  _acceptTx_insertSpendTxs(kvdb_, txLog2->txHash_, tx);

  // 处理地址交易
  _accpetTx_insertAddressTxs(kvdb_, txLog2, addressBalance);

  // 处理未确认计数器和记录
  addUnconfirmedTxPool(txLog2);

  // notification logs
//  writeNotificationLogs(addressBalance, txLog2);
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

void Parser::_getAddressTxNode(const string &address, const uint256 &txhash,
                               AddressTxNode *node) {
  string key;
  string value;

  //
  // 22_{address}_{tx_hash}
  //
  key = Strings::Format("%s%s_%s", KVDB_PREFIX_ADDR_TX_INDEX,
                        address.c_str(), txhash.ToString().c_str());
  kvdb_.get(key, value);
  if (value.size() == 0) {
    THROW_EXCEPTION_DBEX("can't find kv item by key: %s", key.c_str());
  }
  const int32_t index = atoi(value.c_str());

  //
  // 21_{address}_{010index}
  //
  key = Strings::Format("%s%s_%010d", KVDB_PREFIX_ADDR_TX, address.c_str(), index);
  kvdb_.get(key, value);
  auto addressTx = flatbuffers::GetRoot<fbe::AddressTx>(value.data());

  node->txHeight_    = addressTx->tx_height();
  node->balanceDiff_ = addressTx->balance_diff();
  node->idx_         = index;
  node->ymd_         = addressTx->ymd();
}

void Parser::_confirmTx_MoveToFirstUnconfirmed(const string &address,
                                               const int32_t idx1, const int32_t idx2) {
  // idx2 是即将确认的节点
  vector<string> keys;
  vector<string> values;

  keys.push_back(Strings::Format("%s%s_%010d", KVDB_PREFIX_ADDR_TX, address.c_str(), idx1));
  keys.push_back(Strings::Format("%s%s_%010d", KVDB_PREFIX_ADDR_TX, address.c_str(), idx2));
  kvdb_.multiGet(keys, values);
  auto addressTx1 = flatbuffers::GetRoot<fbe::AddressTx>((void *)values[0].data());
  auto addressTx2 = flatbuffers::GetRoot<fbe::AddressTx>((void *)values[1].data());

  // switch item
  kvdb_.set(keys[0], values[1]);
  kvdb_.set(keys[1], values[0]);

  // 22_{address}_{tx_hash}
  kvdb_.set(Strings::Format("%s%s_%s", KVDB_PREFIX_ADDR_TX_INDEX, address.c_str(), addressTx1->tx_hash()->c_str()),
            Strings::Format("%d", idx2));
  kvdb_.set(Strings::Format("%s%s_%s", KVDB_PREFIX_ADDR_TX_INDEX, address.c_str(), addressTx2->tx_hash()->c_str()),
            Strings::Format("%d", idx1));
}

void Parser::_confirmAddressTxNode(const string &address, AddressTxNode *node, AddressInfo *addr) {
  //
  // 变更地址未确认额度等信息
  //
  {
    const int64_t received = (node->balanceDiff_ > 0 ? node->balanceDiff_ : 0);
    const int64_t sent     = (node->balanceDiff_ < 0 ? node->balanceDiff_ * -1 : 0);

    addr->unconfirmedReceived_ -= received;
    addr->unconfirmedSent_     -= sent;
    addr->unconfirmedTxCount_--;
    addr->lastConfirmedTxIdx_   = node->idx_;
  }

  // update AddressTxNode
  {
    const string key = Strings::Format("%s%s_%010d", KVDB_PREFIX_ADDR_TX, address.c_str(), node->idx_);
    string value;
    kvdb_.get(key, value);
    auto addressTx = flatbuffers::GetMutableRoot<fbe::AddressTx>((void *)value.data());
    addressTx->mutate_ymd(node->ymd_);
    addressTx->mutate_tx_height(node->txHeight_);
    kvdb_.set(key, value);
  }
}

// 确认一个交易
void Parser::confirmTx(class TxLog2 *txLog2) {
  //
  // confirm: 变更为确认（设定块高度值等），若不是紧接着上个已确认的交易，则调整交易链。
  //
  auto addressBalance = getTxAddressBalance(txLog2->tx_);

  for (auto &it : addressBalance) {
    const string address      = it.first;

    AddressInfo *addrInfo = _getAddressInfo(kvdb_, address);
    AddressTxNode currNode;  // 当前节点

    // 保障确认的节点在上一个确认节点的后方
    while (1) {
      _getAddressTxNode(address, txLog2->txHash_, &currNode);

      // 涉及移动节点的情形: 即将确认的节点不在上一个确认节点的后方，即中间掺杂了其他交易
      // 若前面无节点，idx 应该挨着
      if (addrInfo->lastConfirmedTxIdx_ + 1 == currNode.idx_) {
        break;
      }
      // 当前节点交换至第一个未确认节点 (仅会执行一次)
      _confirmTx_MoveToFirstUnconfirmed(address, addrInfo->lastConfirmedTxIdx_ + 1, currNode.idx_);
    }

    // 更新即将确认的节点字段
    currNode.ymd_      = txLog2->ymd_;
    currNode.txHeight_ = txLog2->blkHeight_;

    // 确认
    _confirmAddressTxNode(address, &currNode, addrInfo);
  } /* /for */

  //
  // update tx object
  //
  {
    const string key = Strings::Format("%s%s", KVDB_PREFIX_TX_OBJECT, txLog2->txHash_.ToString().c_str());
    string value;
    kvdb_.get(key, value);
    auto txObject = flatbuffers::GetMutableRoot<fbe::Tx>((void *)value.data());

    // confirm时知道高度值，重新计算 coinbase tx fee
    if (txLog2->tx_.IsCoinBase()) {
      const int64_t fee = txLog2->tx_.GetValueOut() - GetBlockValue(txLog2->blkHeight_, 0);
      txObject->mutate_fee(fee);
    }
    txObject->mutate_height(txLog2->blkHeight_);

    kvdb_.set(key, value);
  }

  // 处理未确认计数器和记录
  removeUnconfirmedTxPool(txLog2);
}

// unconfirm tx (address node)
void Parser::_unconfirmAddressTxNode(AddressTxNode *node, AddressInfo *addr) {
//  string sql;
//
//  //
//  // 更新 address_txs_<yyyymm>.tx_height
//  //
//  sql = Strings::Format("UPDATE `address_txs_%d` SET `tx_height`=0,"
//                        " `total_received`=0,`balance_final`=0,`updated_at`='%s' "
//                        " WHERE `address_id`=%lld AND `tx_id`=%lld ",
//                        tableIdx_AddrTxs(node->ymd_), date("%F %T").c_str(),
//                        node->addressId_, node->txId_);
//  dbExplorer_.updateOrThrowEx(sql, 1);
//
//  //
//  // 变更地址未确认额度等信息
//  //
//  const int64_t received = (node->balanceDiff_ > 0 ? node->balanceDiff_ : 0);
//  const int64_t sent     = (node->balanceDiff_ < 0 ? node->balanceDiff_ * -1 : 0);
//
//  AddressTxNode prevNode;
//  if (node->prevTxId_) {
//    _getAddressTxNode(node->prevTxId_, addr, &prevNode);
//  }
//
//  sql = Strings::Format("UPDATE `addresses_%04d` SET "
//                        " `unconfirmed_received` = `unconfirmed_received` + %lld,"
//                        " `unconfirmed_sent`     = `unconfirmed_sent`     + %lld,"
//                        " `unconfirmed_tx_count` = `unconfirmed_tx_count` + 1,  "
//                        " `last_confirmed_tx_ymd`=%d, `last_confirmed_tx_id`=%lld, "
//                        " `updated_at`='%s' WHERE `id`=%lld ",
//                        tableIdx_Addr(addr->addrId_),
//                        received, sent,
//                        (node->prevTxId_ ? prevNode.ymd_  : 0),
//                        (node->prevTxId_ ? prevNode.txId_ : 0),
//                        date("%F %T").c_str(), addr->addrId_);
//  dbExplorer_.updateOrThrowEx(sql, 1);
//
//  addr->unconfirmedReceived_ += received;
//  addr->unconfirmedSent_     += sent;
//  addr->unconfirmedTxCount_++;
//  addr->lastConfirmedTxId_  = (node->prevTxId_ ? prevNode.txId_ : 0);
//  addr->lastConfirmedTxYmd_ = (node->prevTxId_ ? prevNode.ymd_  : 0);
}

// 取消交易的确认
void Parser::unconfirmTx(class TxLog2 *txLog2) {
//  //
//  // unconfirm: 解除最后一个已确认的交易（重置块高度零等），必然是最近确认交易，不涉及调整交易链
//  //
//  string sql;
//
//  // 拿到关联地址的余额变更记录，可能需要调整交易链
//  auto addressBalance = _getTxAddressBalance(txLog2->txHash_, txLog2->tx_);
//
//  for (auto &it : *addressBalance) {
//    const int64_t addrID       = it.first;
//    AddressInfo *addr = _getAddressInfo(dbExplorer_, addrID);
//    AddressTxNode currNode;
//    _getAddressTxNode(txLog2->txId_, addr, &currNode);
//
//    // 移动数据节点，从当前的至 UNCONFIRM_TX_YMD
//    _updateTxNodeYmd(addr, &currNode, UNCONFIRM_TX_YMD);
//    _getAddressTxNode(txLog2->txId_, addr, &currNode);
//    assert(currNode.ymd_ == UNCONFIRM_TX_YMD);
//
//    // 反确认
//    _unconfirmAddressTxNode(&currNode, addr);
//  } /* /for */
//
//  // table.txs_xxxx，重置高度，不涉及变更YMD
//  sql = Strings::Format("UPDATE `txs_%04d` SET `height`=0 WHERE `tx_id`=%lld ",
//                        tableIdx_Tx(txLog2->txId_), txLog2->txId_);
//  dbExplorer_.updateOrThrowEx(sql, 1);
//
//  // 处理未确认计数器和记录
//  addUnconfirmedTxPool(txLog2);
//
//  // notification logs
////  writeNotificationLogs(*addressBalance, txLog2);
}

// 回滚一个块操作
void Parser::rejectBlock(TxLog2 *txLog2) {
//  string sql;
//
//  // 获取块Raw Hex
//  string blkRawHex;
//  _getBlockRawHexByBlockId(txLog2->blkId_, dbExplorer_, &blkRawHex);
//
//  // 解码Raw Block
//  CBlock blk;
//  if (!DecodeHexBlk(blk, blkRawHex)) {
//    THROW_EXCEPTION_DBEX("decode block hex failure, hex: %s", blkRawHex.c_str());
//  }
//
//  const CBlockHeader header  = blk.GetBlockHeader();  // alias
//  const string prevBlockHash = header.hashPrevBlock.ToString();
//
//  // 移除前向block的next指向
//  sql = Strings::Format("UPDATE `0_blocks` SET `next_block_id`=0, `next_block_hash`=''"
//                        " WHERE `hash` = '%s' ", prevBlockHash.c_str());
//  dbExplorer_.updateOrThrowEx(sql, 1);
//
//  // table.0_blocks
//  sql = Strings::Format("DELETE FROM `0_blocks` WHERE `hash`='%s'",
//                        header.GetHash().ToString().c_str());
//  dbExplorer_.updateOrThrowEx(sql, 1);
//
//  // table.block_txs_xxxx
//  sql = Strings::Format("DELETE FROM `block_txs_%04d` WHERE `block_id`=%lld",
//                        tableIdx_BlockTxs(txLog2->blkId_), txLog2->blkId_);
//  dbExplorer_.updateOrThrowEx(sql, (int32_t)blk.vtx.size());
//
//  // 移除块时间戳
//  blkTs_.popBlock();
}

// 回滚，重新插入未花费记录
void _unremoveUnspentOutputs(MySQLConnection &db,
                             const int32_t blockHeight,
                             const int64_t txId, const int32_t position,
                             const int32_t ymd,
                             map<int64_t, int64_t> &addressBalance) {
//  MySQLResult res;
//  string sql;
//  string tableName;
//  char **row;
//  int n;
//
//  // 获取相关output信息
//  tableName = Strings::Format("tx_outputs_%04d", txId % 100);
//  sql = Strings::Format("SELECT `address_ids`,`value`,`address` FROM `%s` "
//                        " WHERE `tx_id`=%lld AND `position`=%d",
//                        tableName.c_str(), txId, position);
//  db.query(sql, res);
//  row = res.nextRow();
//  assert(res.numRows() == 1);
//
//  // 获取地址
//  const string s      = string(row[0]);
//  const int64_t value = atoi64(row[1]);
//  const string ids    = string(row[2]);
//
//  vector<string> addressIdsStrVec = split(s, '|');
//  n = -1;
//  for (auto &addrIdStr : addressIdsStrVec) {
//    n++;
//    const int64_t addrId = atoi64(addrIdStr.c_str());
//    addressBalance[addrId] += -1 * value;
//
//    // 恢复每一个输出地址的 address_unspent_outputs 记录
//    sql = Strings::Format("INSERT INTO `address_unspent_outputs_%04d`"
//                          " (`address_id`, `tx_id`, `position`, `position2`, `block_height`, `value`, `created_at`)"
//                          " VALUES (%lld, %lld, %d, %d, %d, %lld, '%s') ",
//                          addrId % 10, addrId, txId, position, n, blockHeight,
//                          value, date("%F %T").c_str());
//    db.updateOrThrowEx(sql, 1);
//  }
}

// 回滚交易 inputs
static
void _rejectTxInputs(MySQLConnection &db,
                     class TxLog2 *txLog2,
                     const int32_t ymd,
                     map<int64_t, int64_t> &addressBalance) {
//  const CTransaction &tx = txLog2->tx_;
//  const int32_t blockHeight = txLog2->blkHeight_;
//  const int64_t txId = txLog2->txId_;
//
//  string sql;
//  for (auto &in : tx.vin) {
//    // 非 coinbase 无需处理前向交易
//    if (tx.IsCoinBase()) { continue; }
//
//    uint256 prevHash = in.prevout.hash;
//    int64_t prevTxId = txHash2Id(db, prevHash);
//    int32_t prevPos  = (int32_t)in.prevout.n;
//
//    // 将前向交易标记为未花费
//    sql = Strings::Format("UPDATE `tx_outputs_%04d` SET "
//                          " `spent_tx_id`=0, `spent_position`=-1"
//                          " WHERE `tx_id`=%lld AND `position`=%d "
//                          " AND `spent_tx_id`<>0 AND `spent_position`<>-1 ",
//                          prevTxId % 100, prevTxId, prevPos);
//    db.updateOrThrowEx(sql, 1);
//
//    // 重新插入 address_unspent_outputs_xxxx 相关记录
//    _unremoveUnspentOutputs(db, blockHeight, prevTxId, prevPos, ymd, addressBalance);
//  } /* /for */
//
//  // 删除 table.tx_inputs_xxxx 记录， coinbase tx 也有 tx_inputs_xxxx 记录
//  sql = Strings::Format("DELETE FROM `tx_inputs_%04d` WHERE `tx_id`=%lld ",
//                        txId % 100, txId);
//  db.updateOrThrowEx(sql, (int32_t)tx.vin.size());
}

// 回滚交易 outputs
static
void _rejectTxOutputs(MySQLConnection &db,
                      class TxLog2 *txLog2, const int32_t ymd,
                      map<int64_t, int64_t> &addressBalance) {
//  const CTransaction &tx = txLog2->tx_;
//  const int64_t txId = txLog2->txId_;
//
//  int n;
//  const string now = date("%F %T");
//  set<string> allAddresss;
//  string sql;
//
//  // 提取涉及到的所有地址
//  n = -1;
//  for (auto &out : tx.vout) {
//    n++;
//    txnouttype type;
//    vector<CTxDestination> addresses;
//    int nRequired;
//    if (!ExtractDestinations(out.scriptPubKey, type, addresses, nRequired)) {
//      LOG_WARN("extract destinations failure, txId: %lld, hash: %s, position: %d",
//               txId, tx.GetHash().ToString().c_str(), n);
//      continue;
//    }
//    for (auto &addr : addresses) {  // multiSig 可能由多个输出地址
//      allAddresss.insert(CBitcoinAddress(addr).ToString());
//    }
//  }
//  // 拿到所有地址的id
//  map<string, int64_t> addrMap;
//  GetAddressIds(db, allAddresss, addrMap);
//
//  // 处理输出
//  // (`address_id`, `tx_id`, `position`, `position2`, `block_height`, `value`, `created_at`)
//  n = -1;
//  for (auto &out : tx.vout) {
//    n++;
//    txnouttype type;
//    vector<CTxDestination> addresses;
//    int nRequired;
//    ExtractDestinations(out.scriptPubKey, type, addresses, nRequired);
//
//    // multiSig 可能由多个输出地址: https://en.bitcoin.it/wiki/BIP_0011
//    int i = -1;
//    for (auto &addr : addresses) {
//      i++;
//      const string addrStr = CBitcoinAddress(addr).ToString();
//      const int64_t addrId = addrMap[addrStr];
//      addressBalance[addrId] += out.nValue;
//
//      // 删除： address_unspent_outputs 记录
//      sql = Strings::Format("DELETE FROM `address_unspent_outputs_%04d` "
//                            " WHERE `address_id`=%lld AND `tx_id`=%lld "
//                            " AND `position`=%d AND `position2`=%d ",
//                            tableIdx_AddrUnspentOutput(addrId),
//                            addrId, txId, n, i);
//      db.updateOrThrowEx(sql, 1);
//    }
//  }
//
//  // delete table.tx_outputs_xxxx
//  sql = Strings::Format("DELETE FROM `tx_outputs_%04d` WHERE `tx_id`=%lld",
//                        txId % 100, txId);
//  db.updateOrThrowEx(sql, (int32_t)tx.vout.size());
}


// 移除地址交易节点 (含地址信息变更)
void Parser::_removeAddressTxNode(AddressInfo *addr, AddressTxNode *node) {
//  string sql;
//
//  // 设置倒数第二条记录 next 记录为空, 如果存在的话
//  if (node->prevYmd_ != 0) {
//    assert(node->prevTxId_ != 0);
//
//    sql = Strings::Format("UPDATE `address_txs_%d` SET "
//                          " `next_ymd`=0, `next_tx_id`=0, `updated_at`='%s' "
//                          " WHERE `address_id`=%lld AND `tx_id`=%lld ",
//                          tableIdx_AddrTxs(node->prevYmd_), date("%F %T").c_str(),
//                          addr->addrId_, node->prevTxId_);
//    dbExplorer_.updateOrThrowEx(sql, 1);
//  }
//
//  // 删除最后一个交易节点记录
//  sql = Strings::Format("DELETE FROM `address_txs_%d` WHERE `address_id`=%lld AND `tx_id`=%lld ",
//                        tableIdx_AddrTxs(node->ymd_), addr->addrId_, node->txId_);
//  dbExplorer_.updateOrThrowEx(sql, 1);
//
//  //
//  // 更新地址信息
//  //
//  const int64_t received = (node->balanceDiff_ > 0 ? node->balanceDiff_ : 0);
//  const int64_t sent     = (node->balanceDiff_ < 0 ? node->balanceDiff_ * -1 : 0);
//
//  string sqlBegin = "";  // 是否更新 `begin_tx_ymd`/`begin_tx_id`
//  // 没有节点了，移除的是唯一的节点
//  if (node->prevYmd_ == 0) {
//    assert(node->prevTxId_ == 0);
//    sqlBegin = "`begin_tx_id`=0,`begin_tx_ymd`=0,";
//    addr->beginTxId_  = 0;
//    addr->beginTxYmd_ = 0;
//  }
//
//  // 移除的节点必然是未确认的，需要 unconfirmed_xxxx 余额变更
//  sql = Strings::Format("UPDATE `addresses_%04d` SET `tx_count`=`tx_count`-1, "
//                        " `total_received` = `total_received` - %lld,"
//                        " `total_sent`     = `total_sent`     - %lld,"
//                        " `unconfirmed_received` = `unconfirmed_received` - %lld,"
//                        " `unconfirmed_sent`     = `unconfirmed_sent`     - %lld,"
//                        " `unconfirmed_tx_count` = `unconfirmed_tx_count` - 1, "
//                        " `end_tx_ymd`=%d, `end_tx_id`=%lld, %s `updated_at`='%s' "
//                        " WHERE `id`=%lld ",
//                        tableIdx_Addr(addr->addrId_),
//                        received, sent, received, sent,
//                        node->prevYmd_, node->prevTxId_,
//                        sqlBegin.c_str(),
//                        date("%F %T").c_str(), addr->addrId_);
//  dbExplorer_.updateOrThrowEx(sql, 1);
//
//  addr->txCount_--;
//  addr->totalReceived_ -= received;
//  addr->totalSent_     -= sent;
//  addr->unconfirmedReceived_ -= received;
//  addr->unconfirmedSent_     -= sent;
//  addr->unconfirmedTxCount_--;
//  addr->endTxYmd_ = node->prevYmd_;
//  addr->endTxId_  = node->prevTxId_;
}


// 回滚：变更地址&地址对应交易记录
void Parser::_rejectAddressTxs(class TxLog2 *txLog2,
                               const map<int64_t, int64_t> &addressBalance) {
//  for (auto &it : addressBalance) {
//    const int64_t addrID = it.first;
//    //
//    // 当前要reject的记录不是最后一条记录：需要移动节点
//    //
//    AddressInfo *addr = _getAddressInfo(dbExplorer_, addrID);
//    AddressTxNode currNode;  // 当前节点
//
//    while (1) {
//      _getAddressTxNode(txLog2->txId_, addr, &currNode);
//
//      // 涉及移动节点的情形: 当前节点并非最后一个节点
//      if (addr->endTxId_ == currNode.txId_) {
//        break;
//      }
//
//      // 将本节点交换至交易链末尾
//      _rejectTx_MoveToLastUnconfirmed(addr, &currNode);
//    }
//    assert(addr->endTxId_ == currNode.txId_);
//
//    // 移除本节点 (含地址信息变更)
//    _removeAddressTxNode(addr, &currNode);
//  } /* /for */
}


static
void _rejectTx(MySQLConnection &db, class TxLog2 *txLog2) {
//  string sql = Strings::Format("DELETE FROM `txs_%04d` WHERE `tx_id`=%lld ",
//                               tableIdx_Tx(txLog2->txId_), txLog2->txId_);
//  db.updateOrThrowEx(sql, 1);
}

static
int32_t _getTxYmd(MySQLConnection &db, const int64_t txId) {
//  MySQLResult res;
//  string sql = Strings::Format("SELECT `ymd` FROM `txs_%04d` WHERE `tx_id`=%lld ",
//                               tableIdx_Tx(txId), txId);
//  db.query(sql, res);
//  assert(res.numRows() == 1);
//  char **row = res.nextRow();
//  return atoi(row[0]);
}

// 回滚一个交易, reject 的交易都是未确认的交易
void Parser::rejectTx(class TxLog2 *txLog2) {
//  map<int64_t, int64_t> addressBalance;
//  const int32_t ymd = _getTxYmd(dbExplorer_, txLog2->txId_);
//
//  _rejectTxInputs  (dbExplorer_, txLog2, ymd, addressBalance);
//  _rejectTxOutputs (dbExplorer_, txLog2, ymd, addressBalance);
//  _rejectAddressTxs(txLog2, addressBalance);
//  _rejectTx        (dbExplorer_, txLog2);
//
//  // 处理未确认计数器和记录
//  removeUnconfirmedTxPool(txLog2);
//
//  // notification logs
////  writeNotificationLogs(addressBalance, txLog2);
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


uint256 Parser::blockId2Hash(const int64_t blockId) {
  static map<int64_t, uint256> id2hash_;

  auto it = id2hash_.find(blockId);
  if (it != id2hash_.end()) {
    return it->second;
  }

  MySQLResult res;
  char **row = nullptr;
  string sql;

  sql = Strings::Format("SELECT `block_hash` FROM `0_raw_blocks` "
                        " WHERE `id`=%lld ", blockId);
  dbExplorer_.query(sql, res);
  if (res.numRows() == 0) {
    THROW_EXCEPTION_DBEX("can't find block by blockID(%lld) in table.0_raw_blocks",
                         blockId);
  }
  row = res.nextRow();

  uint256 blockHash(row[0]);
  id2hash_.insert(make_pair(blockId, blockHash));

  return blockHash;
}


