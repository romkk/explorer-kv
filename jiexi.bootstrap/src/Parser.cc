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

#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>

#include <algorithm>
#include <string>
#include <iostream>
#include <fstream>

#include <pthread.h>

#include <boost/filesystem.hpp>
#include <boost/thread.hpp>

#include "Parser.h"
#include "Common.h"
#include "Util.h"

#include "bitcoin/base58.h"
#include "bitcoin/util.h"

#include "explorer_generated.h"

#include "rocksdb/cache.h"
#include "rocksdb/compaction_filter.h"
#include "rocksdb/db.h"
#include "rocksdb/options.h"
#include "rocksdb/slice.h"
#include "rocksdb/table.h"
#include "rocksdb/memtablerep.h"


static KVHandler   *gKVHandler   = nullptr;
static AddrHandler *gAddrHandler = nullptr;
static TxHandler   *gTxHandler   = nullptr;


static void _saveAddrTx(vector<struct AddrInfo>::iterator addrInfo);
static void _saveUnspentOutput(TxInfo &txInfo, int32_t position);


RawBlock::RawBlock(const int32_t height, const char *hex) {
  height_  = height;
  hex_     = strdup(hex);
}
RawBlock::~RawBlock() {
  free(hex_);
}

// 从磁盘读取 raw_block 批量文件
// 0_raw_blocks文件里，高度依次递增
void _loadRawBlockFromDisk(map<int32_t, RawBlock*> &blkCache, const int32_t height) {
  // static vars
  static size_t lastOffset  = 0;
  static size_t lastHeight2 = 0;

  const int32_t KCountPerFile = 10000;  // 每个 0_raw_blocks 的容量
  string dir = Config::GConfig.get("rawdata.dir", "");
  // 尾部添加 '/'
  if (dir.length() == 0) {
    dir = ".";
  }
  if (dir[dir.length()-1] != '/') {
    dir += "/";
  }

  const int32_t height2 = (height / KCountPerFile) * KCountPerFile;
  if (lastHeight2 != height2) {
    lastOffset = 0;
    lastHeight2 = height2;
  }

  const int32_t stopHeight  = (int32_t)Config::GConfig.getInt("raw.max.block.height", -1);
  string datafile = Strings::Format("%d_%d", height2,
                                    (height2 + (KCountPerFile - 1)) > stopHeight ? stopHeight : (height2 + (KCountPerFile - 1)));
  const string fname = Strings::Format("%s%s", dir.c_str(), datafile.c_str());
  LOG_INFO("load raw block file: %s", fname.c_str());
  std::ifstream input(fname);
  if (lastOffset > 0) {
    input.seekg(lastOffset, input.beg);
  }
  std::string line;
  const size_t maxReadSize = 500 * 1024 * 1024;  // max read file

  while (std::getline(input, line)) {
    std::vector<std::string> arr = split(line, '\t', 2);
    // line: height, hex
    const int32_t blkHeight  = atoi(arr[0].c_str());
    blkCache[blkHeight] = new RawBlock(blkHeight, arr[1].c_str());
    if (input.tellg() > lastOffset + maxReadSize) {
      lastOffset = input.tellg();
    }
  }
}

// 从文件读取raw block
void getRawBlockFromDisk(const int32_t height, string *rawHex) {
  // 从磁盘直接读取文件，缓存起来，减少数据库交互
  static map<int32_t, RawBlock*> blkCache;

  map<int32_t, RawBlock*>::const_iterator it = blkCache.find(height);

  if (it == blkCache.end()) {
    // clear data before reload
    for (auto &it2 : blkCache) {
      delete it2.second;
    }
    blkCache.clear();  // clear

    // 载入数据
    LOG_INFO("try load raw block data from disk...");
    _loadRawBlockFromDisk(blkCache, height);

    it = blkCache.find(height);  // refind
  }

  if (it == blkCache.end()) {
    THROW_EXCEPTION_DBEX("can't find rawblock from disk cache, height: %d", height);
  }

  if (rawHex != nullptr) {
    *rawHex  = it->second->hex_;
  }
}


////////////////////////////////////////////////////////////////////////////////
//-------------------------------- AddrHandler ---------------------------------
////////////////////////////////////////////////////////////////////////////////
AddrHandler::AddrHandler(const size_t addrCount, const string &filePreAddr) {
  addrInfo_.resize(addrCount);
  addrCount_ = addrCount;

  std::ifstream f(filePreAddr);
  std::string line;
  for (size_t i = 0; std::getline(f, line); ++i) {
    if (i > addrCount_) {
      THROW_EXCEPTION_DBEX("pre address count not match, i: %lld, addrCount_: %lld", i, addrCount_);
    }
    strncpy(addrInfo_[i].addrStr_, line.c_str(), line.length());
  }
  // sort for binary search
  std::sort(addrInfo_.begin(), addrInfo_.end());
}

AddrHandler::~AddrHandler() {
}

vector<struct AddrInfo>::iterator AddrHandler::find(const string &address) {
  AddrInfo needle;
  strncpy(needle.addrStr_, address.c_str(), 35);  // 地址最长35字符
  vector<struct AddrInfo>::iterator it;

  //
  // 这里必须使用：upper_bound()。因为可以确保存在，前向迭代器必然是目标查找元素。
  //
  it = std::upper_bound(addrInfo_.begin(), addrInfo_.end(), needle);
  if (it > addrInfo_.end() || it <= addrInfo_.begin()) {
    THROW_EXCEPTION_DBEX("AddrHandler can't find AddrInfo by address: %s", address.c_str());
  }
  it--;

//  // assert test
//  if (strncmp(it->addrStr_, address.c_str(), 36) != 0) {
//    THROW_EXCEPTION_DBEX("AddrHandler can't find AddrInfo by address: %s", address.c_str());
//  }

  return it;
}

void AddrHandler::dumpAddressAndTxs() {
  flatbuffers::FlatBufferBuilder fbb;
  fbb.ForceDefaults(true);

  for (auto addr = addrInfo_.begin(); addr != addrInfo_.end(); addr++) {
    // address tx
    _saveAddrTx(addr);

    const string &address = addr->addrStr_;
    const string key = Strings::Format("%s%s", KVDB_PREFIX_ADDR_OBJECT, address.c_str());

    fbe::AddressBuilder addressBuilder(fbb);
    addressBuilder.add_received(addr->received_);
    addressBuilder.add_sent(addr->sent_);
    addressBuilder.add_tx_count(addr->txCount_);
    addressBuilder.add_unconfirmed_tx_count(0);  // 初试化都是确认的交易，无未确认信息
    addressBuilder.add_unconfirmed_received(0);
    addressBuilder.add_unconfirmed_sent(0);
    addressBuilder.add_unspent_tx_count(addr->unspentTxCount_);
    addressBuilder.add_unspent_tx_max_index(addr->unspentTxCount_ - 1);
    addressBuilder.add_last_confirmed_tx_index(addr->txCount_ - 1);
    fbb.Finish(addressBuilder.Finish());

    gKVHandler->set(key, fbb.GetBufferPointer(), fbb.GetSize());
    fbb.Clear();
  }
}


////////////////////////////////////////////////////////////////////////////////
//--------------------------------- TxHandler ----------------------------------
////////////////////////////////////////////////////////////////////////////////
TxHandler::TxHandler(const size_t txCount, const string &file) {
  txInfo_.resize(txCount);
  txCount_ = txCount;

  std::ifstream f(file);
  std::string line;
  for (size_t i = 0; std::getline(f, line); ++i) {
    if (i > txCount_) {
      THROW_EXCEPTION_DBEX("pre tx count not match, i: %lld, txCount_: %lld", i, txCount_);
    }
    txInfo_[i].hash256_ = uint256(line);
    // blockHeight_ 尚未设置，后面设置output时会补上
  }
  // sort for binary search
  std::sort(txInfo_.begin(), txInfo_.end());
}

TxHandler::~TxHandler() {
}

vector<struct TxInfo>::iterator TxHandler::find(const uint256 &hash) {
  TxInfo needle;
  needle.hash256_ = hash;
  vector<struct TxInfo>::iterator it;

  //
  // 这里必须使用：upper_bound()。因为可以确保存在，前向迭代器必然是目标查找元素。
  //
  it = std::upper_bound(txInfo_.begin(), txInfo_.end(), needle);
  if (it > txInfo_.end() || it <= txInfo_.begin()) {
    THROW_EXCEPTION_DBEX("TxHandler can't find TxInfo by hash: %s", hash.ToString().c_str());
  }
  it--;
  if (it->hash256_ != hash) {
    THROW_EXCEPTION_DBEX("TxHandler can't find TxInfo by hash: %s", hash.ToString().c_str());
  }
  return it;
}

vector<struct TxInfo>::iterator TxHandler::find(const string &hashStr) {
  return find(uint256(hashStr));
}

void TxHandler::addOutputs(const CTransaction &tx, const int32_t height) {
  vector<struct TxInfo>::iterator it = find(tx.GetHash());
  it->outputs_      = (TxOutput **)calloc(tx.vout.size(), sizeof(TxOutput *));
  it->outputsCount_ = (int32_t)tx.vout.size();
  it->blockHeight_  = height;

  int32_t n = -1;
  for (auto &out : tx.vout) {
    n++;
    TxOutput *ptr = new TxOutput();
    *(it->outputs_ + n) = ptr;

    // script
    ptr->scriptHex_ = HexStr(out.scriptPubKey.begin(), out.scriptPubKey.end());
    ptr->scriptAsm_ = out.scriptPubKey.ToString();

    // asm大小超过1MB, 且大于hex的4倍，则认为asm是非法的，置空
    // output Hex奇葩的交易：
    // http://tbtc.blockr.io/tx/info/c333a53f0174166236e341af9cad795d21578fb87ad7a1b6d2cf8aa9c722083c
    if (ptr->scriptAsm_.length() > 1024*1024 &&
        ptr->scriptAsm_.length() > 4 * ptr->scriptHex_.length()) {
      ptr->scriptAsm_ = "";
    }
    ptr->value_ = out.nValue;

    // 解析出输出的地址
    string addressStr;
    string addressIdsStr;
    txnouttype type;
    vector<CTxDestination> addresses;
    int nRequired;
    if (!ExtractDestinations(out.scriptPubKey, type, addresses, nRequired)) {
      LOG_WARN("extract destinations failure, hash: %s, position: %d",
               tx.GetHash().ToString().c_str(), n);
    }

    // type
    ptr->typeStr_ = GetTxnOutputType(type) ? GetTxnOutputType(type) : "";

    // address, address_ids
    for (auto &addr : addresses) {
      const string s = CBitcoinAddress(addr).ToString();
      ptr->address_.push_back(s);
    }
  }
}

void TxHandler::delOutput(TxInfo &txInfo, const int32_t n) {
  if (txInfo.outputs_ == nullptr || *(txInfo.outputs_ + n) == nullptr) {
    THROW_EXCEPTION_DBEX("already delete output: %s,%d",
                         txInfo.hash256_.ToString().c_str(), n);
  }
  delete *(txInfo.outputs_ + n);
  *(txInfo.outputs_ + n) = nullptr;

  // 检测是否释放整个tx的output部分. 很多tx的所有输出是花掉的状态，free之尽量回收内存
  bool isEmpty = true;
  for (size_t i = 0; i < txInfo.outputsCount_; i++) {
    if (*(txInfo.outputs_ + i) != nullptr) {
      isEmpty = false;
      break;
    }
  }
  if (isEmpty) {
    free(txInfo.outputs_);
    txInfo.outputs_ = nullptr;
  }
}

void TxHandler::delOutputAll(TxInfo &txInfo) {
  if (txInfo.outputs_ == nullptr) {
    THROW_EXCEPTION_DBEX("already delete output: %s",
                         txInfo.hash256_.ToString().c_str());
  }
  for (size_t i = 0; i < txInfo.outputsCount_; i++) {
    if (*(txInfo.outputs_ + i) != nullptr) {
      delete *(txInfo.outputs_ + i);
      *(txInfo.outputs_ + i) = nullptr;
    }
  }
  free(txInfo.outputs_);
  txInfo.outputs_ = nullptr;
}

void TxHandler::delOutput(const uint256 &hash, const int32_t n) {
  delOutput(*find(hash), n);
}

class TxOutput *TxHandler::getOutput(const uint256 &hash, const int32_t n) {
  auto it = find(hash);
  if (it->outputs_ == nullptr || *(it->outputs_ + n) == nullptr) {
    THROW_EXCEPTION_DBEX("can't get output: %s,%d", hash.ToString().c_str(), n);
  }
  return *(it->outputs_ + n);
}

void _saveUnspentOutput(TxInfo &txInfo, int32_t position) {
  TxOutput *out = *(txInfo.outputs_ + position);
  assert(out != nullptr);
  const uint256 &hash = txInfo.hash256_;

  flatbuffers::FlatBufferBuilder fbb;
  fbb.ForceDefaults(true);

  // 遍历处理，可能含有多个地址
  for (size_t i = 0; i < out->address_.size(); i++) {
    const string &address = out->address_[i];
    vector<struct AddrInfo>::iterator addrInfo = gAddrHandler->find(address);
    addrInfo->unspentTxCount_++;
    const int32_t addressUnspentIndex = addrInfo->unspentTxCount_ - 1;

    //
    // 24_{address}_{tx_hash}_{position}
    //
    {
      const string key24 = Strings::Format("%s%s_%s_%d", KVDB_PREFIX_ADDR_UNSPENT_INDEX,
                                           address.c_str(), hash.ToString().c_str(), (int32_t)position);
      fbe::AddressUnspentIdxBuilder addressUnspentIdxBuilder(fbb);
      addressUnspentIdxBuilder.add_index(addressUnspentIndex);
      fbb.Finish(addressUnspentIdxBuilder.Finish());
      gKVHandler->set(key24, fbb.GetBufferPointer(), fbb.GetSize());
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
      addressUnspentBuilder.add_position2((int32_t)i);
      addressUnspentBuilder.add_tx_hash(fb_spentHash);
      addressUnspentBuilder.add_value(out->value_);
      fbb.Finish(addressUnspentBuilder.Finish());
      gKVHandler->set(key23, fbb.GetBufferPointer(), fbb.GetSize());
      fbb.Clear();
    }
  }
}

void TxHandler::dumpUnspentOutputToFile() {
  // 遍历整个tx区，将未花费的数据写入文件
  for (auto &it : txInfo_) {
    if (it.outputs_ == nullptr) {
      continue;
    }
    for (int32_t i = 0; i < it.outputsCount_; i++) {
      if (*(it.outputs_ + i) == nullptr) {
        continue;
      }
      _saveUnspentOutput(it, i);
    }
    delOutputAll(it);
  }
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


////////////////////////////////////////////////////////////////////////////////
//--------------------------------- KVHandler ----------------------------------
////////////////////////////////////////////////////////////////////////////////

KVHandler::KVHandler(): running_(true), startTime_(0), totalStartTime_(0),
counter_(0), totalCounter_(0), totalSize_(0)
{
  options_.compression = rocksdb::kSnappyCompression;
  options_.create_if_missing        = true;   // create the DB if it's not already present
  options_.disableDataSync          = true;   // disable syncing of data files

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

  options_.write_buffer_size = (size_t)64 * 1024 * 1024;
  options_.max_write_buffer_number  = 8;
  options_.disable_auto_compactions = true;

  // params_bulkload
  options_.max_background_compactions = 16;
  options_.max_background_flushes = 7;
  options_.level0_file_num_compaction_trigger = (size_t)10 * 1024 * 1024;
  options_.level0_slowdown_writes_trigger     = (size_t)10 * 1024 * 1024;
  options_.level0_stop_writes_trigger         = (size_t)10 * 1024 * 1024;

  // memtable_factory: vector
  options_.memtable_factory.reset(new rocksdb::VectorRepFactory);

  // initialize BlockBasedTableOptions
  auto cache1 = rocksdb::NewLRUCache(1 * 1024 * 1024 * 1024);
  auto cache2 = rocksdb::NewLRUCache(1 * 1024 * 1024 * 1024);
  rocksdb::BlockBasedTableOptions bbt_opts;
  bbt_opts.block_cache = cache1;
  bbt_opts.block_cache_compressed = cache2;
  bbt_opts.cache_index_and_filter_blocks = 0;
  bbt_opts.block_size = 32 * 1024;
  bbt_opts.format_version = 2;
  options_.table_factory.reset(rocksdb::NewBlockBasedTableFactory(bbt_opts));

  string kvDir  = Config::GConfig.get("rocksdb.ouput.dir", ".");
  if (kvDir.length() && *std::end(kvDir) == '/') {
    kvDir.resize(kvDir.length() - 1);
  }
  const string kvpath = Strings::Format("%s/rocksdb_bootstrap_%d", kvDir.length() ? kvDir.c_str() : ".",
                                        (int32_t)Config::GConfig.getInt("raw.max.block.height", -1));
  LOG_DEBUG("rocksdb path: %s", kvpath.c_str());
  rocksdb::Status s = rocksdb::DB::Open(options_, kvpath, &db_);
  if (!s.ok()) {
    THROW_EXCEPTION_DBEX("open rocks db fail");
  }

  // Optimize
  writeOptions_.disableWAL = true;   // disable Write Ahead Log
  writeOptions_.sync       = false;  // use Asynchronous Writes

  startTime_ = totalStartTime_ = Time::CurrentTimeMill();
}

KVHandler::~KVHandler() {
  delete db_;  // close kv db
  printSpeed();
}

void KVHandler::printSpeed() {
  LOG_INFO("items: %lld, size: %lld M, "
           "speed: %lld / %lld",
           totalCounter_, (int64_t)(totalSize_ * 1.0 / 1024 / 1024),
           (int64_t)(counter_ * 1000.0 / (double)(Time::CurrentTimeMill() - startTime_)),
           (int64_t)(totalCounter_ * 1000.0 / (double)(Time::CurrentTimeMill() - totalStartTime_)))
  startTime_ = Time::CurrentTimeMill();
  counter_ = 0;
}

void KVHandler::set(const string &key, const string &value) {
  set(key, (const uint8_t *)value.data(), value.size());
}

void KVHandler::set(const string &key, const uint8_t *data, const size_t size) {
  const rocksdb::Slice skey  (key.data(), key.size());
  const rocksdb::Slice svalue((const char *)data, size);

  db_->Put(writeOptions_, skey, svalue);

  counter_++;
  totalCounter_++;
  totalSize_ += size;
  if (counter_ % 100000 == 0 && Time::CurrentTimeMill() > startTime_) {
    printSpeed();
  }
}

void KVHandler::compact() {
  LogScope ls("KVHandler::compact");
  db_->CompactRange(rocksdb::CompactRangeOptions(), nullptr, nullptr);
}


////////////////////////////////////////////////////////////////////////////////
//--------------------------------- PreParser ----------------------------------
////////////////////////////////////////////////////////////////////////////////
PreParser::PreParser(): blkTs_(2016) {
  stopHeight_  = (int32_t)Config::GConfig.getInt("raw.max.block.height", -1);
  filePreTx_   = Config::GConfig.get("pre.tx.output.file", "");
  filePreAddr_ = Config::GConfig.get("pre.address.output.file", "");
  txCount_ = addrCount_ = 0;
  addrHandler_ = nullptr;
  curHeight_  = 0;
  running_ = true;

  memset(&blockInfo_, 0, sizeof(blockInfo_));
}

PreParser::~PreParser() {
  stop();

  // close
  delete addrHandler_;
  delete txHandler_;

  // close kv
  delete gKVHandler;

  gTxHandler   = nullptr;
  gKVHandler   = nullptr;
  gAddrHandler = nullptr;
}

void PreParser::stop() {
  if (!running_) {
    return;
  }
  running_ = false;
  LOG_INFO("stop PreParser...");
}

void PreParser::init() {
  LOG_INFO("get tx / address size...");
  addrCount_ = getNumberOfLines(filePreAddr_);
  txCount_   = getNumberOfLines(filePreTx_);
  if (addrCount_ == 0 || txCount_ == 0) {
    THROW_EXCEPTION_DBEX("number of line PreTx(%lld) or PreAddr(%lld) invalid",
                         txCount_, addrCount_);
  }
  LOG_INFO("tx count: %lld, address count: %lld", txCount_, addrCount_);

  // init
  {
    LogScope ls("init address Handler");
    addrHandler_ = new AddrHandler(addrCount_, filePreAddr_);
    gAddrHandler = addrHandler_;
  }
  {
    LogScope ls("init txs Handler");
    txHandler_   = new TxHandler(txCount_, filePreTx_);
    gTxHandler = txHandler_;
  }

  // kv handler
  gKVHandler = new KVHandler();
}

void PreParser::_saveBlock(const BlockInfo &b) {
  flatbuffers::FlatBufferBuilder fbb;
  fbb.ForceDefaults(true);

  const uint64_t pdiff = TargetToPdiff(b.blockHash_);
  auto fb_mrklRoot    = fbb.CreateString(b.header_.hashMerkleRoot.ToString());
  auto fb_nextBlkHash = fbb.CreateString(b.nextBlockHash_.ToString());
  auto fb_prevBlkHash = fbb.CreateString(b.header_.hashPrevBlock.ToString());

  fbe::BlockBuilder blkBuilder(fbb);
  blkBuilder.add_bits(b.header_.nBits);
  blkBuilder.add_created_at((uint32_t)time(nullptr));
  blkBuilder.add_difficulty(b.diff_);
  blkBuilder.add_height(b.height_);
  blkBuilder.add_mrkl_root(fb_mrklRoot);
  blkBuilder.add_next_block_hash(fb_nextBlkHash);
  blkBuilder.add_nonce(b.header_.nNonce);
  blkBuilder.add_pool_difficulty(pdiff);
  blkBuilder.add_prev_block_hash(fb_prevBlkHash);
  blkBuilder.add_reward_block(b.rewardBlock_);
  blkBuilder.add_reward_fees(b.rewardFee_);
  blkBuilder.add_size(b.size_);
  blkBuilder.add_timestamp(b.header_.nTime);
  blkBuilder.add_tx_count(b.txCount_);
  blkBuilder.add_version(b.header_.nVersion);
  blkBuilder.add_is_orphan(false);
  blkBuilder.add_curr_max_timestamp((uint32_t)blkTs_.getMaxTimestamp());
  fbb.Finish(blkBuilder.Finish());

  // 11_{block_hash}, 需紧接 blockBuilder.Finish()
  const string key11 = Strings::Format("%s%s", KVDB_PREFIX_BLOCK_OBJECT,
                                       b.blockHash_.ToString().c_str());
  gKVHandler->set(key11, fbb.GetBufferPointer(), fbb.GetSize());
  fbb.Clear();

  // 10_{block_height}
  const string key10 = Strings::Format("%s%010d", KVDB_PREFIX_BLOCK_HEIGHT, b.height_);
  gKVHandler->set(key10, b.blockHash_.ToString());

  // 插入 14_{010timestamp}_{010height}
  {
    const string key = Strings::Format("%s%010u_%010d", KVDB_PREFIX_BLOCK_TIMESTAMP,
                                       (uint32_t)blkTs_.getMaxTimestamp(), b.height_);
    gKVHandler->set(key, Strings::Format("%s", b.blockHash_.ToString().c_str()));
  }
}

void PreParser::parseBlock(const CBlock &blk, const int32_t height, const int32_t blockBytes) {
  CBlockHeader header = blk.GetBlockHeader();  // alias

  blkTs_.pushBlock(height, header.GetBlockTime());

  BlockInfo cur;
  cur.blockHash_ = blk.GetHash();
  BitsToDifficulty(header.nBits, cur.diff_);
  cur.header_    = header;
  cur.height_    = height;
  cur.nextBlockHash_ = uint256();
  cur.size_  = blockBytes;
  cur.rewardBlock_ = GetBlockValue(height, 0);
  cur.rewardFee_   = blk.vtx[0].GetValueOut() - cur.rewardBlock_;
  cur.txCount_ = (int32_t)blk.vtx.size();

  if (height > 0) {
    blockInfo_.nextBlockHash_ = cur.blockHash_;
  }

  // 保存
  if (height > 0) {
    _saveBlock(blockInfo_);
  }
  memcpy(&blockInfo_, &cur, sizeof(BlockInfo));

  // 保存最后一个
  if (height == stopHeight_) {
    _saveBlock(blockInfo_);
  }

  // 保存当前块对应的交易
  _insertBlockTxs(blk);
}

void PreParser::_insertBlockTxs(const CBlock &blk) {
  const int32_t kBatchSize = 500;  // 每500条为一个批次

  int32_t i = 0;
  string key;
  string value;
  for (const auto &tx : blk.vtx) {
    value += tx.GetHash().ToString();

    if ((i+1) % kBatchSize == 0) {
      key = Strings::Format("%s%s_%d", KVDB_PREFIX_BLOCK_TXS_STR,
                            blk.GetHash().ToString().c_str(), (int32_t)(i/kBatchSize));
      gKVHandler->set(key, value);
      value.clear();
    }
    i++;
  }

  if (value.size() != 0) {
    key = Strings::Format("%s%s_%d", KVDB_PREFIX_BLOCK_TXS_STR,
                          blk.GetHash().ToString().c_str(), (int32_t)(i/kBatchSize));
    gKVHandler->set(key, value);
    value.clear();
  }
}

void _saveAddrTx(vector<struct AddrInfo>::iterator addrInfo) {
  flatbuffers::FlatBufferBuilder fbb;
  fbb.ForceDefaults(true);

  AddrTx &t = addrInfo->addrTx_;
  const int32_t addressTxIndex = addrInfo->txCount_ - 1;  // index start from 0
  auto fb_txhash = fbb.CreateString(t.txHash_.ToString());

  //
  // AddressTx
  //
  {
    fbe::AddressTxBuilder addressTxBuilder(fbb);
    addressTxBuilder.add_balance_diff(t.balanceDiff_);
    addressTxBuilder.add_tx_hash(fb_txhash);
    addressTxBuilder.add_tx_height(t.txHeight_);
    addressTxBuilder.add_tx_block_time(t.txBlockTime_);
    fbb.Finish(addressTxBuilder.Finish());

    // 21_{address}_{010index}
    const string key21 = Strings::Format("%s%s_%010d", KVDB_PREFIX_ADDR_TX,
                                         addrInfo->addrStr_, addressTxIndex);
    gKVHandler->set(key21, fbb.GetBufferPointer(), fbb.GetSize());
    fbb.Clear();
  }

  //
  // AddressTxIdx
  //
  {
    // 22_{address}_{tx_hash}
    const string key22 = Strings::Format("%s%s_%s", KVDB_PREFIX_ADDR_TX_INDEX,
                                         addrInfo->addrStr_,
                                         t.txHash_.ToString().c_str());
    gKVHandler->set(key22, Strings::Format("%d", addressTxIndex));
  }
}

void PreParser::handleAddressTxs(const map<string, int64_t> &addressBalance,
                                 const int32_t height, const uint32_t blockTime,
                                 const uint256 txHash) {
  for (auto &it : addressBalance) {
    const string &addrStr      = it.first;
    const int64_t &balanceDiff = it.second;

    vector<struct AddrInfo>::iterator addrInfo = addrHandler_->find(addrStr);

    // 更新上次交易，如果有的话
    if (addrInfo->addrTx_.txBlockTime_ != 0) {
      // save last one
      _saveAddrTx(addrInfo);
    }

    // 记录当前交易信息
    struct AddrTx cur;
    cur.txHeight_     = height;
    cur.balanceDiff_  = balanceDiff;
    cur.txBlockTime_  = blockTime;
    cur.txHash_       = txHash;

    //
    // 更新地址信息
    //
    const int64_t balanceReceived = (balanceDiff > 0 ? balanceDiff : 0);
    const int64_t balanceSent     = (balanceDiff < 0 ? balanceDiff * -1 : 0);

    // 变更地址信息，无未确认相关数据
    addrInfo->received_ += balanceReceived;
    addrInfo->sent_     += balanceSent;
    addrInfo->txCount_++;

    // switch
    memcpy(&(addrInfo->addrTx_), &cur, sizeof(struct AddrTx));
  }
}

void PreParser::parseTx(const int32_t height, const CTransaction &tx,
                        const uint32_t blockNTime) {
  const uint256 txHash = tx.GetHash();

  // 硬编码特殊交易处理
  //
  // 1. tx hash: d5d27987d2a3dfc724e359870c6644b40e497bdc0589a033220fe15429d88599
  // 该交易在两个不同的高度块(91812, 91842)中出现过
  // 91842块中有且仅有这一个交易
  //
  // 2. tx hash: e3bf3d07d4b0375638d5f1db5255fe07ba2c4cb067cd81b84ee974b6585fb468
  // 该交易在两个不同的高度块(91722, 91880)中出现过
  if ((height == 91842 &&
       txHash == uint256("d5d27987d2a3dfc724e359870c6644b40e497bdc0589a033220fe15429d88599")) ||
      (height == 91880 &&
       txHash == uint256("e3bf3d07d4b0375638d5f1db5255fe07ba2c4cb067cd81b84ee974b6585fb468")))
  {
    LOG_WARN("ignore tx, height: %d, hash: %s", height, txHash.ToString().c_str());
    return;
  }

  int64_t valueIn = 0;  // 交易的输入之和，遍历交易后才能得出
  map<string/* address */, int64_t/* balance_diff */> addressBalance;

  flatbuffers::FlatBufferBuilder fbb;
  fbb.ForceDefaults(true);
  vector<flatbuffers::Offset<fbe::TxInput > > fb_txInputs;
  vector<flatbuffers::Offset<fbe::TxOutput> > fb_txOutputs;

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

      if (tx.IsCoinBase()) {
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
        // 获取前向交易
        uint256 prevHash = in.prevout.hash;
        prevPostion  = (int32_t)in.prevout.n;
        auto pTxInfo = txHandler_->find(prevHash);
        TxOutput *poutput = *(pTxInfo->outputs_ + prevPostion);
        prevValue    = poutput->value_;

        //
        // 生成被消费记录
        //
        // 02_{tx_hash}_{position}
        {
          // TODO: 剥离为函数
          flatbuffers::FlatBufferBuilder fbb2;
          fbb2.ForceDefaults(true);
          string key = Strings::Format("%s%s_%d", KVDB_PREFIX_TX_SPEND,
                                       prevHash.ToString().c_str(), (int32_t)prevPostion);

          auto fb_spentHash = fbb2.CreateString(txHash.ToString());
          fbe::TxSpentByBuilder txSpentByBuilder(fbb2);
          txSpentByBuilder.add_position(n);
          txSpentByBuilder.add_tx_hash(fb_spentHash);
          fbb2.Finish(txSpentByBuilder.Finish());
          gKVHandler->set(key, fbb2.GetBufferPointer(), fbb2.GetSize());
          fbb2.Clear();
        }

        valueIn += prevValue;

        for (auto j = 0; j < poutput->address_.size(); j++) {
          const string address = poutput->address_[j];
          addressBalance[address] += prevValue * -1;
          fb_addressesVec.push_back(fbb.CreateString(address));
        }
        fb_prevTxHash    = fbb.CreateString(in.prevout.hash.ToString());
        fb_ScriptAsm     = fbb.CreateString(in.scriptSig.ToString());
        prevPostion = in.prevout.n;

        // 可以删除前向输入了，因每个前向输入只会使用一次
        txHandler_->delOutput(prevHash, prevPostion);
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

  const int64_t valueOut = tx.GetValueOut();
  int64_t fee = 0;
  if (tx.IsCoinBase()) {
    fee = valueOut - GetBlockValue(height, 0);  // coinbase的fee为 block rewards
  } else {
    fee = valueIn - valueOut;
  }

  // ouputs
  txHandler_->addOutputs(tx, height);

  // get tx bytes
  CDataStream ssTx(SER_NETWORK, BITCOIN_PROTOCOL_VERSION);
  ssTx << tx;
  const string txHex = HexStr(ssTx.begin(), ssTx.end());

  //
  // build tx object
  //
  fbe::TxBuilder txBuilder(fbb);
  txBuilder.add_block_height(height);
  txBuilder.add_block_time(blockNTime);
  txBuilder.add_is_coinbase(tx.IsCoinBase());
  txBuilder.add_version(tx.nVersion);
  txBuilder.add_lock_time(tx.nLockTime);
  txBuilder.add_size((int)(txHex.length()/2));
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
  {
    const string key = KVDB_PREFIX_TX_OBJECT + txHash.ToString();
    gKVHandler->set(key, fbb.GetBufferPointer(), fbb.GetSize());
    fbb.Clear();
  }

  // 处理地址变更
  handleAddressTxs(addressBalance, height, blockNTime, txHash);
}

void PreParser::run() {
  // 解析
  while (running_) {
    if (curHeight_ > stopHeight_) {
      LOG_INFO("reach max height: %d", stopHeight_);
      break;
    }

    string blkRawHex;
    getRawBlockFromDisk(curHeight_, &blkRawHex);

    // 解码Raw Hex
    vector<unsigned char> blockData(ParseHex(blkRawHex));
    CDataStream ssBlock(blockData, SER_NETWORK, BITCOIN_PROTOCOL_VERSION);
    CBlock blk;
    try {
      ssBlock >> blk;
    }
    catch (std::exception &e) {
      THROW_EXCEPTION_DBEX("Block decode failed, height: %d", curHeight_);
    }

    // 处理块
    LOG_INFO("parse block, height: %6d, txs: %5lld", curHeight_, blk.vtx.size());
    parseBlock(blk, curHeight_, (int32_t)blkRawHex.length()/2);

    // 处理交易
    for (auto &tx : blk.vtx) {
      parseTx(curHeight_, tx, blk.nTime);
    }

    curHeight_++;
  }

  if (running_) {
    // 清理数据：未花费的output，必须在输出地址之前执行
    LogScope ls("dump unspent output to file");
    txHandler_->dumpUnspentOutputToFile();
  }

  if (running_) {
    // 清理数据：地址数据, 地址最后关联的交易
    LogScope ls("dump address and txs");
    addrHandler_->dumpAddressAndTxs();
  }
}

