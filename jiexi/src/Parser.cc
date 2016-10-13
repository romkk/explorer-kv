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
#include <chrono>
#include <iostream>
#include <fstream>

#include <boost/thread.hpp>
#include <boost/filesystem.hpp>

#include "Parser.h"
#include "Common.h"
#include "Util.h"

#include "bitcoin/base58.h"
#include "bitcoin/core_io.h"
#include "bitcoin/chainparams.h"
#include "bitcoin/util.h"
#include "bitcoin/utilstrencodings.h"

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
maxBlkTimestamp_(0), ymd_(0), txId_(0), txHash_()
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

bool TxLog2::isEmpty() {
  if (txHash_ == uint256()) {
    return true;
  }
  return false;
}


/////////////////////////////////  AddressTxNode  ///////////////////////////////////
AddressTxNode::AddressTxNode() {
  reset();
}

AddressTxNode::AddressTxNode(const AddressTxNode &node) {
  memcpy(&txBlockTime_, &node.txBlockTime_, sizeof(AddressTxNode));
}

void AddressTxNode::reset() {
  memset(&txBlockTime_, 0, sizeof(AddressTxNode));
  txHeight_ = -1;
  idx_ = -1;
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
Parser::Parser(): running_(true), unconfirmedTxsSize_(0), unconfirmedTxsCount_(0),
kvdb_(Config::GConfig.get("rocksdb.path", "")), notifyProducer_(nullptr), currBlockHeight_(-1), txlogsBuffer_(200),
recognizeBlock_(Config::GConfig.get("pools.json", ""), &kvdb_)
{
  notifyFileLog2Producer_ = Config::GConfig.get("notify.log2producer.file");
  if (notifyFileLog2Producer_.empty()) {
    THROW_EXCEPTION_DBEX("empty config: notify.log2producer.file");
  }

  //
  // notification.dir
  //
  {
    string dir = Config::GConfig.get("notification.dir", ".");
    if (*(std::prev(dir.end())) == '/') {  // remove last '/'
      dir.resize(dir.length() - 1);
    }
    notifyBeginHeight_ = Config::GConfig.getInt("notification.begin.height", -1);
    notifyProducer_ = new NotifyProducer(dir);
    notifyProducer_->init();
  }

  if (!recognizeBlock_.loadConfigJson()) {
    THROW_EXCEPTION_DBEX("load pool json fail");
  }
}

Parser::~Parser() {
  stop();

  LOG_INFO("stop block recognize thread...");
  if (threadRecognizeBlock_.joinable()) {
    threadRecognizeBlock_.join();
  }

  LOG_INFO("stop txlogs produce thread...");
  if (threadProduceTxlogs_.joinable()) {
    threadProduceTxlogs_.join();
  }

  LOG_INFO("stop txlogs comsumer thread...");
  if (threadHandleTxlogs_.joinable()) {
    threadHandleTxlogs_.join();
  }

  LOG_INFO("stop watch notify thread...");
  if (threadWatchNotify_.joinable()) {
    threadWatchNotify_.join();
  }

  LOG_INFO("stop notify producer...");
  if (notifyProducer_ != nullptr) {
    delete notifyProducer_;
    notifyProducer_ = nullptr;
  }
}

void Parser::stop() {
  if (running_ == false) { return; }

  running_ = false;
  LOG_INFO("stop tparser...");

  // 填入一个空的 txlogs ，使得 popback 时，不会卡住，线程可顺利退出
  {
    TxLog2 txlog2;
    txlogsBuffer_.pushFront(txlog2);
  }

  apiServer_.stop();
  inotify_.RemoveAll();
  newTxlogs_.notify_all();
}

bool Parser::init() {
  // try connect mysql db
  MySQLConnection dbExplorer(Config::GConfig.get("db.explorer.uri"));
  if (!dbExplorer.ping()) {
    LOG_FATAL("connect to explorer DB failure");
    return false;
  }

  // 检测DB参数： max_allowed_packet
  const int32_t maxAllowed = atoi(dbExplorer.getVariable("max_allowed_packet").c_str());
  const int32_t kMinAllowed = 8*1024*1024;
  if (maxAllowed < kMinAllowed) {
    LOG_FATAL("mysql.db.max_allowed_packet(%d) is too small, should >= %d",
              maxAllowed, kMinAllowed);
    return false;
  }

  kvdb_.open();

  //
  // 90_tparser_unconfirmed_txs_count, 90_tparser_unconfirmed_txs_size
  //
  {
    string key;
    string value;

    // unconfirmedTxsCount_
    key = "90_tparser_unconfirmed_txs_count";
    kvdb_.getMayNotExist(key, value);
    if (value.size() != 0) {
      unconfirmedTxsCount_ = atoi(value.c_str());
    }

    key = "90_tparser_unconfirmed_txs_size";
    kvdb_.getMayNotExist(key, value);
    if (value.size() != 0) {
      unconfirmedTxsSize_ = atoi64(value.c_str());
    }
  }

  // 启动监听文件（log2）线程
  threadWatchNotify_ = thread(&Parser::threadWatchNotifyFile, this);

  // 启动 txlogs2 生产线程
  threadProduceTxlogs_ = thread(&Parser::threadProduceTxlogs, this);

  // 启动 txlogs2 消费线程
  threadHandleTxlogs_ = thread(&Parser::threadHandleTxlogs, this);

  // 启动块识别线程
  threadRecognizeBlock_ = thread(&Parser::threadRecognizeBlock, this);

  return true;
}

void Parser::threadRecognizeBlock() {
  // 启动时，需要识别块
  bool isRecognizeAll = Config::GConfig.getBool("pools.recognize.all.at.start", false);
  recognizeBlock_.recognizeBlocks(99999999, 0, isRecognizeAll ? false : true);
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
        newTxlogs_.notify_all();
      }
    } /* /while */
  } catch (InotifyException &e) {
    THROW_EXCEPTION_DBEX("Inotify exception occured: %s", e.GetMessage().c_str());
  }
}

void Parser::run() {
  // http服务采用libevent，由于只能在当前线程结束，所以放入主线程里

  // 启动API Server Http服务
  apiServer_.setKVDB(&kvdb_);
  apiServer_.init();  // setup evhtp
  apiServer_.run();
}

void Parser::threadHandleTxlogs() {
  string blockHash;  // 目前仅用在URL回调上

  while (running_) {
    TxLog2 txLog2;

    txlogsBuffer_.popBack(&txLog2);
    if (txLog2.isEmpty()) {
      LOG_WARN("txlog2 is empty");
      continue;
    }

    LOG_INFO("process txlog2, logId: %d, type: %d, "
             "height: %d, tx hash: %s, created: %s",
             txLog2.id_, txLog2.type_, txLog2.blkHeight_,
             txLog2.txHash_.ToString().c_str(), txLog2.createdAt_.c_str());

    if (txLog2.type_ == LOG2TYPE_TX_ACCEPT) {
      acceptTx(&txLog2);
    }
    else if (txLog2.type_ == LOG2TYPE_TX_CONFIRM) {
      // confirm 时才能 acceptBlock()
      if (txLog2.tx_.IsCoinBase()) {
        acceptBlock(&txLog2, blockHash);
        // 触发块识别
        recognizeBlock_.recognizeBlocks(txLog2.blkHeight_, txLog2.blkHeight_, true);
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

    // 更新最后消费成功 ID
    updateLastTxlog2Id(txLog2.id_);

    writeLastProcessTxlogTime();

    if (blockHash.length()) {  // 目前仅用在URL回调上，仅使用一次
      callBlockRelayParseUrl(blockHash);
      blockHash.clear();
    }
  } /* /while */

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

void Parser::updateLastTxlog2Id(const int64_t newId) {
  const string key = "90_tparser_txlogs_offset_id";
  string value = Strings::Format("%lld", newId);
  kvdb_.set(key, value);
}

void _insertBlock(KVDB &kvdb, const CBlock &blk, const int32_t height,
                  const int32_t blockSize, uint32_t maxBlkTimestamp) {
  CBlockHeader header = blk.GetBlockHeader();  // alias
  const uint256 blockHash    = blk.GetHash();
  const string blockHashStr  = blockHash.ToString();
  const string prevBlockHash = header.hashPrevBlock.ToString();
  flatbuffers::FlatBufferBuilder fbb;
  fbb.ForceDefaults(true);

  double difficulty = 0;
  BitsToDifficulty(header.nBits, &difficulty);
  const uint64_t pdiff = TargetToDiff(blockHash);

  const CChainParams& chainparams = Params();
  const int64_t rewardBlock = GetBlockSubsidy(height, chainparams.GetConsensus());
  const int64_t rewardFees  = blk.vtx[0].GetValueOut() - rewardBlock;
  assert(rewardFees >= 0);

  {
    auto fb_mrklRoot = fbb.CreateString(header.hashMerkleRoot.ToString());
    // next hash需要"填零"，保持长度一致, 当下一个块来临时，直接对那块内存进行改写操作
    auto fb_nextBlkHash = fbb.CreateString(uint256().ToString());
    auto fb_prevBlkHash = fbb.CreateString(prevBlockHash);

    fbe::BlockBuilder blockBuilder(fbb);
    blockBuilder.add_height(height);
    blockBuilder.add_bits(header.nBits);
    blockBuilder.add_created_at((uint32_t)time(nullptr));
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
    blockBuilder.add_is_orphan(false);
    blockBuilder.add_curr_max_timestamp(maxBlkTimestamp);
    fbb.Finish(blockBuilder.Finish());

    // 11_{block_hash}, 需紧接 blockBuilder.Finish()
    const string key11 = Strings::Format("%s%s", KVDB_PREFIX_BLOCK_OBJECT, blockHash.ToString().c_str());
    kvdb.set(key11, fbb.GetBufferPointer(), fbb.GetSize());
    fbb.Clear();
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
  MySQLConnection dbExplorer(Config::GConfig.get("db.explorer.uri"));

  // 获取块Raw Hex
  string blkRawHex;
  _getBlockRawHexByBlockId(txLog2->blkId_, dbExplorer, &blkRawHex);

  // 解码Raw Hex
  CBlock blk;
  if (!DecodeHexBlk(blk, blkRawHex)) {
    THROW_EXCEPTION_DBEX("decode block hex failure, hex: %s", blkRawHex.c_str());
  }
  blockHash = blk.GetHash().ToString();
  LOG_INFO("accept block: %d, %s", txLog2->blkHeight_, blockHash.c_str());

  // 插入块数据
  _insertBlock(kvdb_, blk, txLog2->blkHeight_, (int32_t)blkRawHex.length()/2, txLog2->maxBlkTimestamp_);

  // 插入块内交易数据
  _insertBlockTxs(kvdb_, blk);

  //
  // 移除 orphan block 信息, KVDB_PREFIX_BLOCK_ORPHAN
  //
  {
    const string key = Strings::Format("%s%010d", KVDB_PREFIX_BLOCK_ORPHAN, txLog2->blkHeight_);
    string value;
    if (kvdb_.getMayNotExist(key, value) == true /* exist */) {
      _removeOrphanBlockHash(blk.GetHash().ToString(), value);
      if (value.size() > 0) {
        kvdb_.set(key, value);
      } else {
        kvdb_.del(key);
      }
    }
  }

  // 插入 14\_{010timestamp}_{010height}
  {
    const string key = Strings::Format("%s%010u_%010d", KVDB_PREFIX_BLOCK_TIMESTAMP,
                                       txLog2->maxBlkTimestamp_, txLog2->blkHeight_);
    kvdb_.set(key, Strings::Format("%s", blk.GetHash().ToString().c_str()));
  }

  // 设置当前块高度
  currBlockHeight_ = txLog2->blkHeight_;

  // 写入事件通知
  {
    string sbuf;
    NotifyItem nitem;
    nitem.loadblock(NOTIFY_EVENT_BLOCK_ACCEPT, blk.GetHash(), txLog2->blkHeight_);
    sbuf.append(nitem.toStr() + "\n");
    notifyProducer_->write(sbuf);
  }
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

    kvdb.getMayNotExist(key, value);
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
                                   fbAddress->unspent_tx_max_index(),
                                   fbAddress->last_confirmed_tx_index(),
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
  fbb.ForceDefaults(true);

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
      auto txhash = fbb.CreateString(txLog2->txHash_.ToString());
      fbe::AddressTxBuilder addressTxBuilder(fbb);
      addressTxBuilder.add_balance_diff(balanceDiff);
      addressTxBuilder.add_tx_hash(txhash);
      addressTxBuilder.add_tx_height(-1);
      addressTxBuilder.add_tx_block_time(txLog2->maxBlkTimestamp_);  // TODO: block time
      fbb.Finish(addressTxBuilder.Finish());

      // 21_{address}_{010index}
      const string key21 = Strings::Format("%s%s_%010d", KVDB_PREFIX_ADDR_TX,
                                           address.c_str(), addressTxIndex);
      kvdb.set(key21, fbb.GetBufferPointer(), fbb.GetSize());
      fbb.Clear();
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
    kvdb_.set(key, value);
  }

  // unconfirmedTxsSize_
  {
    const string key = "90_tparser_unconfirmed_txs_size";
    const string value = Strings::Format("%lld", unconfirmedTxsSize_);
    kvdb_.set(key, value);
  }
}

void Parser::removeUnconfirmedTxPool(class TxLog2 *txLog2) {
  unconfirmedTxsCount_--;
  unconfirmedTxsSize_ -= txLog2->txHex_.length() / 2;

  // unconfirmedTxsCount_
  {
    const string key = "90_tparser_unconfirmed_txs_count";
    const string value = Strings::Format("%d", unconfirmedTxsCount_);
    kvdb_.set(key, value);
  }

  // unconfirmedTxsSize_
  {
    const string key = "90_tparser_unconfirmed_txs_size";
    const string value = Strings::Format("%lld", unconfirmedTxsSize_);
    kvdb_.set(key, value);
  }
}

bool Parser::isWriteNotificationLogs() {
  if (notifyProducer_ == nullptr ||
      (currBlockHeight_ != -1 && notifyBeginHeight_ != -1 && currBlockHeight_ < notifyBeginHeight_)) {
    // 当前高度未达到时，不启动事件通知
    return false;
  }
  return true;
}

// 写入通知日志文件
void Parser::writeNotificationLogs(const map<string, int64_t> &addressBalance,
                                   class TxLog2 *txLog2) {
  static string buffer;
  static NotifyItem item;

  if (!isWriteNotificationLogs()) {
    return;
  }

  buffer.clear();
  for (auto it : addressBalance) {
    int32_t type = 0;
    switch (txLog2->type_) {
      case LOG2TYPE_TX_ACCEPT:
        type = NOTIFY_EVENT_TX_ACCEPT;
        break;
      case LOG2TYPE_TX_CONFIRM:
        type = NOTIFY_EVENT_TX_CONFIRM;
        break;
      case LOG2TYPE_TX_UNCONFIRM:
        type = NOTIFY_EVENT_TX_UNCONFIRM;
        break;
      case LOG2TYPE_TX_REJECT:
        type = NOTIFY_EVENT_TX_REJECT;
        break;
      default:
        break;
    }
    item.loadtx(type, it.first, txLog2->txHash_, txLog2->blkHeight_, it.second);
    buffer.append(item.toStr() + "\n");
  }
  notifyProducer_->write(buffer);
}

// 插入交易的raw hex
static void _acceptTx_insertRawHex(KVDB &kvdb, const string &txHex, const uint256 &hash) {
//  const string key = KVDB_PREFIX_TX_RAW_HEX + hash.ToString();
//  if (kvdb.keyExist(key)) {
//    return;  // already exist
//  }
//
//  vector<char> buff;
//  Hex2Bin(txHex.c_str(), txHex.length(), buff);
//  kvdb.set(key, (uint8_t *)buff.data(), buff.size());
}

// 插入交易object对象
static void _acceptTx_insertTxObject(KVDB &kvdb, const uint256 &hash,
                                     flatbuffers::FlatBufferBuilder *fbb) {
  const string key = KVDB_PREFIX_TX_OBJECT + hash.ToString();
  kvdb.set(key, fbb->GetBufferPointer(), fbb->GetSize());
  fbb->Clear();
}

// 移除prev unspent outputs
static void _acceptTx_removeUnspentOutputs(KVDB &kvdb, const uint256 &hash, const CTransaction &tx,
                                           vector<const fbe::TxOutput *> &prevTxOutputs) {
  assert(prevTxOutputs.size() == tx.vin.size());

  int n = -1;

  for (auto fb_txoutput : prevTxOutputs) {
    n++;
    auto addresses = fb_txoutput->addresses();
    const uint256 &prevHash   = tx.vin[n].prevout.hash;
    const int32_t prevPostion = tx.vin[n].prevout.n;

    // 绝大部分只有一个地址，但存在多个地址可能，所以不得不采用循环
    for (flatbuffers::uoffset_t j = 0; j < addresses->size(); j++) {
      const string address = addresses->operator[](j)->str();

      AddressInfo *addr = _getAddressInfo(kvdb, address);
      addr->unspentTxCount_--;  // 地址未花费数量减一

      // 某个地址的未花费index
      // 24_{address}_{tx_hash}_{position}
      const string key24 = Strings::Format("%s%s_%s_%d", KVDB_PREFIX_ADDR_UNSPENT_INDEX,
                            address.c_str(), prevHash.ToString().c_str(), (int32_t)prevPostion);
      string value;
      kvdb.get(key24, value);
      auto unspentOutputIdx = flatbuffers::GetRoot<fbe::AddressUnspentIdx>(value.data());

      // 删除之
      // 23_{address}_{010index}
      const string key23 = Strings::Format("%s%s_%010d", KVDB_PREFIX_ADDR_UNSPENT,
                                           address.c_str(), unspentOutputIdx->index());
      kvdb.del(key23);  // 删除unspent list node
      kvdb.del(key24);  // 删除索引关系
    }
  }
}

// 生成被消费记录
static void _acceptTx_insertSpendTxs(KVDB &kvdb, const uint256 &hash, const CTransaction &tx) {
  flatbuffers::FlatBufferBuilder fbb;
  fbb.ForceDefaults(true);

  string key;

  int n = -1;  // postion
  for (const auto &in : tx.vin) {
    n++;
    const uint256 &prevHash = in.prevout.hash;
    // 02_{tx_hash}_{position}
    key = Strings::Format("%s%s_%d", KVDB_PREFIX_TX_SPEND,
                          prevHash.ToString().c_str(), (int32_t)in.prevout.n);

    auto fb_spentHash = fbb.CreateString(hash.ToString());
    fbe::TxSpentByBuilder txSpentByBuilder(fbb);
    txSpentByBuilder.add_position(n);
    txSpentByBuilder.add_tx_hash(fb_spentHash);
    fbb.Finish(txSpentByBuilder.Finish());
    kvdb.set(key, fbb.GetBufferPointer(), fbb.GetSize());
    fbb.Clear();
  }
}

//
// 插入地址的 unspent 记录: reject tx时，也会用到，恢复未花费记录
//
static void _acceptTx_insertAddressUnspent(KVDB &kvdb, const int64_t nValue,
                                           const string &address, const uint256 &hash,
                                           const int32_t position, const int32_t position2) {
  flatbuffers::FlatBufferBuilder fbb;
  fbb.ForceDefaults(true);

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
    fbb.Finish(addressUnspentIdxBuilder.Finish());
    kvdb.set(key24, fbb.GetBufferPointer(), fbb.GetSize());
    fbb.Clear();
  }

  //
  // 23_{address}_{010index}
  //
  {
    const string key23 = Strings::Format("%s%s_%010d", KVDB_PREFIX_ADDR_UNSPENT,
                                         address.c_str(), addressUnspentIndex);

    auto fb_spentHash = fbb.CreateString(hash.ToString());
    fbe::AddressUnspentBuilder addressUnspentBuilder(fbb);
    addressUnspentBuilder.add_position(position);
    addressUnspentBuilder.add_position2(position2);
    addressUnspentBuilder.add_tx_hash(fb_spentHash);
    addressUnspentBuilder.add_value(nValue);
    fbb.Finish(addressUnspentBuilder.Finish());
    kvdb.set(key23, fbb.GetBufferPointer(), fbb.GetSize());
    fbb.Clear();
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
  const CTransaction &tx = txLog2->tx_;

  // 硬编码特殊交易处理
  //
  // 1. tx hash: d5d27987d2a3dfc724e359870c6644b40e497bdc0589a033220fe15429d88599
  // 该交易在两个不同的高度块(91812, 91842)中出现过
  // 91842块中有且仅有这一个交易
  //
  // 2. tx hash: e3bf3d07d4b0375638d5f1db5255fe07ba2c4cb067cd81b84ee974b6585fb468
  // 该交易在两个不同的高度块(91722, 91880)中出现过
  if ((txLog2->blkHeight_ == 91842 &&
       txLog2->txHash_ == uint256S("d5d27987d2a3dfc724e359870c6644b40e497bdc0589a033220fe15429d88599")) ||
      (txLog2->blkHeight_ == 91880 &&
       txLog2->txHash_ == uint256S("e3bf3d07d4b0375638d5f1db5255fe07ba2c4cb067cd81b84ee974b6585fb468"))) {
    LOG_WARN("ignore tx, height: %d, hash: %s",
             txLog2->blkHeight_, txLog2->txHash_.ToString().c_str());
    return;
  }

  int64_t valueIn = 0;  // 交易的输入之和，遍历交易后才能得出
  vector<const fbe::TxOutput *> prevTxOutputs;
  vector<string> prevTxsData;  // prevTxsData 必须一直存在，否则 prevTxOutputs 读取无效内存
  map<string/* address */, int64_t/* balance_diff */> addressBalance;

  flatbuffers::FlatBufferBuilder fbb;
  fbb.ForceDefaults(true);
  vector<flatbuffers::Offset<fbe::TxInput > > fb_txInputs;
  vector<flatbuffers::Offset<fbe::TxOutput> > fb_txOutputs;

  // 读取前向交易的输出
  if (!txLog2->tx_.IsCoinBase()) {
    kvdb_.getPrevTxOutputs(tx, prevTxsData, prevTxOutputs);
  }

  //
  // inputs
  //
  {
    int n = -1;
    for (const auto &in : tx.vin) {
      n++;
      auto fb_ScriptHex = fbb.CreateString(HexStr(in.scriptSig.begin(), in.scriptSig.end()));
      vector<flatbuffers::Offset<flatbuffers::String> > fb_addressesVec;
      flatbuffers::Offset<flatbuffers::String> fb_ScriptAsm;
      flatbuffers::Offset<flatbuffers::String> fb_prevTxHash;
      int64_t prevValue;
      int32_t prevPostion;

      if (txLog2->tx_.IsCoinBase()) {
        // 插入当前交易的inputs, coinbase tx的 scriptSig 不做decode，可能含有非法字符
        // 通常无法解析成功, 不解析 scriptAsm
        // coinbase无法担心其长度，bitcoind对coinbase tx的coinbase字段长度做了限制
        fb_ScriptAsm     = fbb.CreateString("");
        fb_prevTxHash    = fbb.CreateString(uint256().ToString());
        prevValue = 0;
        prevPostion = -1;
      }
      else
      {
        prevValue = prevTxOutputs[n]->value();
        valueIn += prevValue;

        auto addresses = prevTxOutputs[n]->addresses();
        for (flatbuffers::uoffset_t j = 0; j < addresses->size(); j++) {
          const string address = addresses->operator[](j)->str();
          addressBalance[address] += prevValue * -1;
          fb_addressesVec.push_back(fbb.CreateString(address));
        }
        fb_prevTxHash = fbb.CreateString(in.prevout.hash.ToString());
        fb_ScriptAsm  = fbb.CreateString(ScriptToAsmStr(in.scriptSig, true));
        prevPostion   = in.prevout.n;
      }

      auto fb_prevAddresses = fbb.CreateVector(fb_addressesVec);
      fbe::TxInputBuilder txInputBuilder(fbb);
      txInputBuilder.add_script_asm(fb_ScriptAsm);
      txInputBuilder.add_script_hex(fb_ScriptHex);
      txInputBuilder.add_sequence(in.nSequence);
      txInputBuilder.add_prev_tx_hash(fb_prevTxHash);
      txInputBuilder.add_prev_position(prevPostion);
      txInputBuilder.add_prev_value(prevValue);
      txInputBuilder.add_prev_addresses(fb_prevAddresses);
      fb_txInputs.push_back(txInputBuilder.Finish());
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
      string outputScriptAsm = ScriptToAsmStr(out.scriptPubKey, true);
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
  fbe::TxBuilder txBuilder(fbb);
  txBuilder.add_block_height(-1);
  txBuilder.add_block_time(0);
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
  txBuilder.add_created_at((uint32_t)time(nullptr));
  txBuilder.add_is_double_spend(false);
  fbb.Finish(txBuilder.Finish());
  // insert tx object, 需要紧跟 txBuilder.Finish() 函数，否则 fbb 内存会破坏
  _acceptTx_insertTxObject(kvdb_, txLog2->txHash_, &fbb);

//  // insert tx raw hex，实际插入binary data，减小一半体积
//  _acceptTx_insertRawHex(kvdb_, txLog2->txHex_, txLog2->txHash_);

  if (!txLog2->tx_.IsCoinBase()) {
    // remove unspent，移除未花费交易，扣减计数器
    _acceptTx_removeUnspentOutputs(kvdb_, txLog2->txHash_, tx, prevTxOutputs);

    // insert spent txs, 插入前向交易的花费记录
    _acceptTx_insertSpendTxs(kvdb_, txLog2->txHash_, tx);
  }

  // 处理地址交易
  _accpetTx_insertAddressTxs(kvdb_, txLog2, addressBalance);

  // 新增未确认交易
  {
    const string key03 = Strings::Format("%s%s", KVDB_PREFIX_TX_UNCONFIRMED,
                                         txLog2->txHash_.ToString().c_str());
    auto fb_txhash = fbb.CreateString(txLog2->txHash_.ToString());
    fbe::UnconfirmedTxBuilder unconfirmedTxBuilder(fbb);
    unconfirmedTxBuilder.add_fee(fee);
    unconfirmedTxBuilder.add_size((int)(txLog2->txHex_.length()/2));
    unconfirmedTxBuilder.add_tx_hash(fb_txhash);
    fbb.Finish(unconfirmedTxBuilder.Finish());
    kvdb_.set(key03, fbb.GetBufferPointer(), fbb.GetSize());
    fbb.Clear();
  }

  // 刷入地址变更信息
  flushAddressInfo(addressBalance);

  // 处理未确认计数器和记录
  addUnconfirmedTxPool(txLog2);

  // notification logs
  writeNotificationLogs(addressBalance, txLog2);
}

void Parser::flushAddressInfo(const map<string, int64_t> &addressBalance) {
  flatbuffers::FlatBufferBuilder fbb;
  fbb.ForceDefaults(true);

  for (const auto &it : addressBalance) {
    const string &address = it.first;
    AddressInfo *addr = _getAddressInfo(kvdb_, address);

    fbe::AddressBuilder addressBuilder(fbb);
    addressBuilder.add_received(addr->received_);
    addressBuilder.add_sent(addr->sent_);
    addressBuilder.add_tx_count(addr->txCount_);
    addressBuilder.add_unconfirmed_tx_count(addr->unconfirmedTxCount_);
    addressBuilder.add_unconfirmed_received(addr->unconfirmedReceived_);
    addressBuilder.add_unconfirmed_sent(addr->unconfirmedSent_);
    addressBuilder.add_unspent_tx_count(addr->unspentTxCount_);
    addressBuilder.add_unspent_tx_max_index(addr->unspentTxIndex_);
    addressBuilder.add_last_confirmed_tx_index(addr->lastConfirmedTxIdx_);
    fbb.Finish(addressBuilder.Finish());

    const string key = Strings::Format("%s%s", KVDB_PREFIX_ADDR_OBJECT, address.c_str());
    kvdb_.set(key, fbb.GetBufferPointer(), fbb.GetSize());
    fbb.Clear();
  }
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
  node->txBlockTime_ = addressTx->tx_block_time();
}

// 交换两个交易节点
void Parser::_switchTxNode(const string &address, const int32_t idx1, const int32_t idx2) {
  vector<string> keys;
  vector<string> values;

  // 21_{address}_{010index}
  keys.push_back(Strings::Format("%s%s_%010d", KVDB_PREFIX_ADDR_TX, address.c_str(), idx1));
  keys.push_back(Strings::Format("%s%s_%010d", KVDB_PREFIX_ADDR_TX, address.c_str(), idx2));
  kvdb_.multiGet(keys, values);
  auto addressTx1 = flatbuffers::GetRoot<fbe::AddressTx>((void *)values[0].data());
  auto addressTx2 = flatbuffers::GetRoot<fbe::AddressTx>((void *)values[1].data());

  //
  // switch item
  //
  // 21_{address}_{010index}
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
    addressTx->mutate_tx_block_time(node->txBlockTime_);
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
      _switchTxNode(address, addrInfo->lastConfirmedTxIdx_ + 1, currNode.idx_);
    }

    // 更新即将确认的节点字段
    currNode.txBlockTime_ = txLog2->maxBlkTimestamp_;  // TODO: block time
    currNode.txHeight_    = txLog2->blkHeight_;

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
      const CChainParams& chainparams = Params();
      const int64_t rewardBlock = GetBlockSubsidy(txLog2->blkHeight_, chainparams.GetConsensus());

      const int64_t fee = txLog2->tx_.GetValueOut() - rewardBlock;
      txObject->mutate_fee(fee);
    }
    txObject->mutate_block_height(txLog2->blkHeight_);
    txObject->mutate_block_time(txLog2->maxBlkTimestamp_);

    kvdb_.set(key, value);
  }

  // 移除未确认交易
  {
    const string key03 = Strings::Format("%s%s", KVDB_PREFIX_TX_UNCONFIRMED,
                                         txLog2->txHash_.ToString().c_str());
    kvdb_.del(key03);
  }

  // 刷入地址变更信息
  flushAddressInfo(addressBalance);

  // 处理未确认计数器和记录
  removeUnconfirmedTxPool(txLog2);

  // notification logs
  writeNotificationLogs(addressBalance, txLog2);
}

// unconfirm tx (address node)
void Parser::_unconfirmAddressTxNode(const string &address, AddressTxNode *node, AddressInfo *addr) {
  //
  // 变更地址未确认额度等信息
  //
  {
    const int64_t received = (node->balanceDiff_ > 0 ? node->balanceDiff_ : 0);
    const int64_t sent     = (node->balanceDiff_ < 0 ? node->balanceDiff_ * -1 : 0);

    addr->unconfirmedReceived_ += received;
    addr->unconfirmedSent_     += sent;
    addr->unconfirmedTxCount_++;
    addr->lastConfirmedTxIdx_   -= 1;  // 全部未确认则为 -1
    assert(addr->lastConfirmedTxIdx_ >= -1);
    assert(node->idx_ - 1 == addr->lastConfirmedTxIdx_);
  }

  // update AddressTxNode
  {
    const string key = Strings::Format("%s%s_%010d", KVDB_PREFIX_ADDR_TX, address.c_str(), node->idx_);
    string value;
    kvdb_.get(key, value);
    auto addressTx = flatbuffers::GetMutableRoot<fbe::AddressTx>((void *)value.data());
    addressTx->mutate_tx_block_time(0);
    addressTx->mutate_tx_height(-1);
    kvdb_.set(key, value);
  }
}

// 取消交易的确认
void Parser::unconfirmTx(class TxLog2 *txLog2) {
  //
  // unconfirm: 解除最后一个已确认的交易（重置块高度零等），必然是最近确认交易，不涉及调整交易链
  //

  auto addressBalance = getTxAddressBalance(txLog2->tx_);
  for (auto &it : addressBalance) {
    const string address      = it.first;

    AddressInfo *addrInfo = _getAddressInfo(kvdb_, address);
    AddressTxNode currNode;  // 当前节点
    _getAddressTxNode(address, txLog2->txHash_, &currNode);

    // 反确认
    _unconfirmAddressTxNode(address, &currNode, addrInfo);
  }

  //
  // update tx object
  //
  {
    const string key = Strings::Format("%s%s", KVDB_PREFIX_TX_OBJECT, txLog2->txHash_.ToString().c_str());
    string value;
    kvdb_.get(key, value);
    auto txObject = flatbuffers::GetMutableRoot<fbe::Tx>((void *)value.data());
    txObject->mutate_block_time(0);
    txObject->mutate_block_height(-1);
    kvdb_.set(key, value);

    // 新增未确认交易
    flatbuffers::FlatBufferBuilder fbb;
    const string key03 = Strings::Format("%s%s", KVDB_PREFIX_TX_UNCONFIRMED,
                                         txLog2->txHash_.ToString().c_str());
    auto fb_txhash = fbb.CreateString(txLog2->txHash_.ToString());
    fbe::UnconfirmedTxBuilder unconfirmedTxBuilder(fbb);
    unconfirmedTxBuilder.add_fee(txObject->fee());
    unconfirmedTxBuilder.add_size(txObject->size());
    unconfirmedTxBuilder.add_tx_hash(fb_txhash);
    fbb.Finish(unconfirmedTxBuilder.Finish());
    kvdb_.set(key03, fbb.GetBufferPointer(), fbb.GetSize());
    fbb.Clear();
  }

  // 刷入地址变更信息
  flushAddressInfo(addressBalance);

  // 处理未确认计数器和记录
  addUnconfirmedTxPool(txLog2);

  // notification logs
  writeNotificationLogs(addressBalance, txLog2);
}

// 移除一个字符串（64，哈希）
void _removeOrphanBlockHash(const string &hash, string &value) {
  set<string> blks;
  for (auto i = 0; i < value.length() / 64; i++) {
    blks.insert(value.substr(i * 64, 64));
  }
  blks.erase(hash);

  value.clear();
  for (const auto &it : blks) {
    value.insert(value.end(), it.begin(), it.begin() + 64);
  }
}

// 插入一个字符串（64，哈希）
void _insertOrphanBlockHash(const string &hash, string &value) {
  if (value.length() == 0) {
    value.insert(value.end(), hash.begin(), hash.begin() + 64);
    return;
  }

  set<string> blks;
  for (auto i = 0; i < value.length() / 64; i++) {
    blks.insert(value.substr(i * 64, 64));
  }
  blks.insert(hash);

  value.clear();
  for (const auto &it : blks) {
    value.insert(value.end(), it.begin(), it.begin() + 64);
  }
}

// 回滚一个块操作
void Parser::rejectBlock(TxLog2 *txLog2) {
  MySQLConnection dbExplorer(Config::GConfig.get("db.explorer.uri"));

  const int32_t height = txLog2->blkHeight_;

  // 获取块Raw Hex
  string blkRawHex;
  _getBlockRawHexByBlockId(txLog2->blkId_, dbExplorer, &blkRawHex);

  // 解码Raw Block
  CBlock blk;
  if (!DecodeHexBlk(blk, blkRawHex)) {
    THROW_EXCEPTION_DBEX("decode block hex failure, hex: %s", blkRawHex.c_str());
  }

  const CBlockHeader header  = blk.GetBlockHeader();  // alias
  const string prevBlockHash = header.hashPrevBlock.ToString();
  const uint256 blockHash = header.GetHash();

  LOG_INFO("reject block: %d, %s", height, blockHash.ToString().c_str());

  // 记录 orphan block 信息, KVDB_PREFIX_BLOCK_ORPHAN
  {
    const string key = Strings::Format("%s%010d", KVDB_PREFIX_BLOCK_ORPHAN, height);
    string value;
    kvdb_.getMayNotExist(key, value);
    _insertOrphanBlockHash(blockHash.ToString(), value);
    kvdb_.set(key, value);
  }

  //
  // 10_{block_height}
  const string key10 = Strings::Format("%s%010d", KVDB_PREFIX_BLOCK_HEIGHT, height);
  kvdb_.del(key10);

  // 更新当前块信息
  {
    const string key11 = Strings::Format("%s%s", KVDB_PREFIX_BLOCK_OBJECT, blockHash.ToString().c_str());
    string value;
    kvdb_.get(key11, value);
    auto fb_block = flatbuffers::GetMutableRoot<fbe::Block>((void *)value.data());

    // 孤块标识位设置为 true
    fb_block->mutate_is_orphan(true);
    kvdb_.set(key11, value);
  }

  // 移除 14\_{010timestamp}_{010height}
  {
    const string key14 = Strings::Format("%s%010u_%010d", KVDB_PREFIX_BLOCK_TIMESTAMP,
                                         txLog2->maxBlkTimestamp_, txLog2->blkHeight_);
    kvdb_.del(key14);
  }

  //
  // 更新前向块信息
  //
  if (height > 0) {
    // 11_{block_hash}
    const string prevBlkKey = Strings::Format("%s%s", KVDB_PREFIX_BLOCK_OBJECT, prevBlockHash.c_str());
    string value;
    kvdb_.get(prevBlkKey, value);

    auto prevBlock = flatbuffers::GetMutableRoot<fbe::Block>((void *)value.data());
    assert(prevBlock->next_block_hash()->size() == 64);
    for (flatbuffers::uoffset_t i = 0; i < 64; i++) {
      prevBlock->mutable_next_block_hash()->Mutate(i, '0');
    }
    kvdb_.set(prevBlkKey, value);
  }

  // 设置当前块高度
  currBlockHeight_ = txLog2->blkHeight_;

  // 写入事件通知
  {
    string sbuf;
    NotifyItem nitem;
    nitem.loadblock(NOTIFY_EVENT_BLOCK_REJECT, blockHash, height);
    sbuf.append(nitem.toStr() + "\n");
    notifyProducer_->write(sbuf);
  }
}


// 移除地址交易节点 (含地址信息变更)
void Parser::_removeAddressTxNode(const string &address, const int32_t idx,
                                  const uint256 &txhash, const int64_t balanceDiff,
                                  AddressInfo *addr) {
  assert(addr->txCount_ == idx + 1);
  const string key21 = Strings::Format("%s%s_%010d", KVDB_PREFIX_ADDR_TX, address.c_str(), idx);
  const string key22 = Strings::Format("%s%s_%s", KVDB_PREFIX_ADDR_TX_INDEX, address.c_str(), txhash);

  kvdb_.del(key21);
  kvdb_.del(key22);

  //
  // 更新地址信息
  //
  const int64_t received = (balanceDiff > 0 ? balanceDiff : 0);
  const int64_t sent     = (balanceDiff < 0 ? balanceDiff * -1 : 0);

  addr->txCount_--;
  addr->received_ -= received;
  addr->sent_     -= sent;
  addr->unconfirmedReceived_ -= received;
  addr->unconfirmedSent_     -= sent;
  addr->unconfirmedTxCount_--;
}


// 回滚：变更地址&地址对应交易记录
void Parser::_rejectAddressTxs(class TxLog2 *txLog2, const map<string, int64_t> &addressBalance) {
  for (auto &it : addressBalance) {
    const string address = it.first;

    AddressInfo *addrInfo = _getAddressInfo(kvdb_, address);
    AddressTxNode currNode;  // 当前节点

    // reject记录必须是最后一条记录，若不是最后一条记录则需要移动节点
    while (1) {
      _getAddressTxNode(address, txLog2->txHash_, &currNode);

      if (currNode.idx_ + 1 == addrInfo->txCount_) {
        break;  // 已经是最后一条记录
      }
      assert(addrInfo->txCount_ >= 2);

      // 当前节点交换至最后一个节点（应仅执行一次）
      _switchTxNode(address, addrInfo->txCount_ - 1, currNode.idx_);
    }

    // 移除本节点 (含地址信息变更)
    _removeAddressTxNode(address, addrInfo->txCount_ - 1, txLog2->txHash_,
                         currNode.balanceDiff_, addrInfo);
  } /* /for */
}


// 回滚一个交易, reject 的交易都是未确认的交易
void Parser::rejectTx(class TxLog2 *txLog2) {
  const CTransaction &tx = txLog2->tx_;
  const uint256 &txHash  = txLog2->txHash_;
  string key;

  vector<const fbe::TxOutput *> prevTxOutputs;
  vector<string> prevTxsData;  // prevTxsData 必须一直存在，否则 prevTxOutputs 读取无效内存
  map<string/* address */, int64_t/* balance_diff */> addressBalance;

  // 读取前向交易的输出
  if (!txLog2->tx_.IsCoinBase()) {
    kvdb_.getPrevTxOutputs(tx, prevTxsData, prevTxOutputs);
  }

  //
  // inputs
  //
  if (!tx.IsCoinBase()) {  // coinbase 无需处理前向交易
    int n = -1;
    for (const auto &in : tx.vin) {
      n++;

      auto addresses = prevTxOutputs[n]->addresses();
      const int64_t prevValue = prevTxOutputs[n]->value();
      const uint256 &prevHash = in.prevout.hash;
      const int32_t prevPos   = (int32_t)in.prevout.n;

      // 将前向交易的花费记录删除
      // 02_{tx_hash}_{position}
      key = Strings::Format("%s%s_%d", KVDB_PREFIX_TX_SPEND,
                            prevHash.ToString().c_str(), (int32_t)prevPos);
      kvdb_.del(key);

      // 重新插入前向输出的未花费记录
      for (auto j = 0; j < addresses->size(); j++) {
        const string address = addresses->operator[](j)->str();
        addressBalance[address] += prevValue * -1;
        _acceptTx_insertAddressUnspent(kvdb_, prevValue, address, prevHash, prevPos, j);
      }
    }
  }

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

      //
      // 删除未花费记录
      //
      for (auto &addrDest : addresses) {
        const string address = CBitcoinAddress(addrDest).ToString();
        addressBalance[address] += out.nValue;

        // 某个地址的未花费index
        // 24_{address}_{tx_hash}_{position}
        const string key24 = Strings::Format("%s%s_%s_%d", KVDB_PREFIX_ADDR_UNSPENT_INDEX,
                                             address.c_str(), txHash.ToString().c_str(), (int32_t)n);
        string value;
        kvdb_.get(key24, value);
        auto unspentOutputIdx = flatbuffers::GetRoot<fbe::AddressUnspentIdx>(value.data());

        // 删除之
        // 23_{address}_{010index}
        const string key23 = Strings::Format("%s%s_%010d", KVDB_PREFIX_ADDR_UNSPENT,
                                             address.c_str(), unspentOutputIdx->index());
        kvdb_.del(key23);  // 删除unspent list node
        kvdb_.del(key24);  // 删除索引关系
      }
    }
  }

  //
  // update tx object
  //
  {
    // 更新 is_double_spend，设置为true
    const string key01 = Strings::Format("%s%s", KVDB_PREFIX_TX_OBJECT, txLog2->txHash_.ToString().c_str());
    string value;
    kvdb_.get(key01, value);
    auto txObject = flatbuffers::GetMutableRoot<fbe::Tx>((void *)value.data());
    txObject->mutate_is_double_spend(true);
    kvdb_.set(key01, value);
  }

  // 移除未确认
  {
    const string key03 = Strings::Format("%s%s", KVDB_PREFIX_TX_UNCONFIRMED,
                                         txLog2->txHash_.ToString().c_str());
    kvdb_.del(key03);
  }

  _rejectAddressTxs(txLog2, addressBalance);

  // 刷入地址变更信息
  flushAddressInfo(addressBalance);

  // 处理未确认计数器和记录
  removeUnconfirmedTxPool(txLog2);

  // notification logs
  writeNotificationLogs(addressBalance, txLog2);
}

// 获取上次 txlog2 的进度ID
int64_t Parser::getLastTxLog2Id() {
  const string key = "90_tparser_txlogs_offset_id";
  string value;
  if (kvdb_.getMayNotExist(key, value) == true /* exist */) {
    return atoi64(value);
  }

  // default value is zero
  updateLastTxlog2Id(0);
  return 0;
}

void Parser::threadProduceTxlogs() {
  MySQLConnection dbExplorer(Config::GConfig.get("db.explorer.uri"));

  int64_t lastTxLog2Id = getLastTxLog2Id();
  LOG_INFO("lastTxLog2Id: %lld", lastTxLog2Id);

  while (running_) {
    TxLog2 txLog2;

    if (tryFetchTxLog2FromDB(&txLog2, lastTxLog2Id, dbExplorer) == false) {
      UniqueLock ul(lock_);
      // 默认等待N毫秒，直至超时，中间有人触发，则立即continue读取记录
      newTxlogs_.wait_for(ul, std::chrono::milliseconds(3*1000));
      continue;
    }

    if (!running_) { break; }

    // inset to buffer
    txlogsBuffer_.pushFront(txLog2);

    lastTxLog2Id = txLog2.id_;
  } /* /while */
}

bool Parser::tryFetchTxLog2FromDB(class TxLog2 *txLog2, const int64_t lastId,
                                  MySQLConnection &dbExplorer) {
  {
    MySQLResult res;
    char **row = nullptr;
    string sql;

    // batch_id 为 -1 表示最后的临时记录
    sql = Strings::Format(" SELECT `id`,`type`,`block_height`,`block_id`, "
                          " `max_block_timestamp`, `tx_hash`,`created_at` "
                          " FROM `0_txlogs2` "
                          " WHERE `id` > %lld AND `batch_id` <> -1 ORDER BY `id` ASC LIMIT 1 ",
                          lastId);

    dbExplorer.query(sql, res);
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
    txLog2->txHash_       = uint256S(row[5]);
    txLog2->createdAt_    = string(row[6]);

    if (!(txLog2->type_ == LOG2TYPE_TX_ACCEPT    ||
          txLog2->type_ == LOG2TYPE_TX_CONFIRM   ||
          txLog2->type_ == LOG2TYPE_TX_UNCONFIRM ||
          txLog2->type_ == LOG2TYPE_TX_REJECT)) {
      LOG_FATAL("invalid type: %d", txLog2->type_);
      return false;
    }
  }

  //
  // accept: 我们把未确认的交易都设置为未来时间: 2030-01-01
  //
  if (txLog2->type_ == LOG2TYPE_TX_ACCEPT) {
    txLog2->ymd_ = UNCONFIRM_TX_YMD;
    txLog2->maxBlkTimestamp_ = UNCONFIRM_TX_TIMESTAMP;
  }

  // find raw tx hex & id
  {
    txInfoCache_.getTxInfo(dbExplorer, txLog2->txHash_,
                           &(txLog2->txId_), &(txLog2->txHex_));
    if (!DecodeHexTx(txLog2->tx_, txLog2->txHex_)) {
      THROW_EXCEPTION_DBEX("TX decode failed, hex: %s", txLog2->txHex_.c_str());
    }
  }

  // 清理旧记录: 保留200万条已经消费过的记录，每2万条触发清理一次
  const int64_t kKeepNum = 200*10000;
  if (txLog2->id_ > kKeepNum && txLog2->id_ % 20000 == 0) {
    string delSql = Strings::Format("DELETE FROM `0_txlogs2` WHERE `id` < %lld",
                                    txLog2->id_ - kKeepNum);
    size_t delRowNum = dbExplorer.update(delSql);
    LOG_INFO("delete expired txlogs2 items: %llu", delRowNum);

    // 清理表: table.0_row_blocks,  144*30=4,320
    delSql = "DELETE FROM `0_raw_blocks` WHERE `id` < "
    " (SELECT * FROM (SELECT (IFNULL(max(`id`), 0) - 4320) as `max_id` FROM `0_raw_blocks`) AS `t1`)";
    delRowNum = dbExplorer.update(delSql);
    LOG_INFO("delete 0_raw_blocks items: %llu", delRowNum);

    // TODO 清理表: table.raw_txs_xxxx
    // 目前可以采用手动清理：
    //  1. 停止 log1producer
    //  2. 保证 log2producer 已经完全消费log1之后，停止 log2producer
    //  3. 等待tparser，等完全消费了log2后，手动清空所有表： table.raw_txs_xxxx
    //  4. 启动log1producer，启动log2producer
  }

  return true;
}



