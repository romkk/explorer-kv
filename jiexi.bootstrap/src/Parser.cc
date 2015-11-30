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

static void _saveAddrTx(vector<struct AddrInfo>::iterator addrInfo);
static void _saveTxOutput(TxInfo &txInfo, int32_t n);

RawBlock::RawBlock(const int64_t blockId, const int32_t height, const int32_t chainId,
                   const uint256 hash, const char *hex) {
  blockId_ = blockId;
  height_  = height;
  chainId_ = chainId;
  hash_    = hash;
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
    dir = "./";
  }
  else if (dir[dir.length()-1] != '/') {
    dir += "/";
  }

  const int32_t height2 = (height / KCountPerFile) * KCountPerFile;
  if (lastHeight2 != height2) {
    lastOffset = 0;
    lastHeight2 = height2;
  }

  const int32_t stopHeight  = (int32_t)Config::GConfig.getInt("raw.max.block.height", -1);
  string path = Strings::Format("%d_%d", height2,
                                (height2 + (KCountPerFile - 1)) > stopHeight ? stopHeight : (height2 + (KCountPerFile - 1)));

  const string fname = Strings::Format("%s%s/0_raw_blocks",
                                       dir.c_str(), path.c_str());
  LOG_INFO("load raw block file: %s", fname.c_str());
  std::ifstream input(fname);
  if (lastOffset > 0) {
    input.seekg(lastOffset, input.beg);
  }
  std::string line;
  const size_t maxReadSize = 500 * 1024 * 1024;  // max read file

  while (std::getline(input, line)) {
    std::vector<std::string> arr = split(line, ',');
    // line: blockId, hash, height, chain_id, hex
    const uint256 blkHash(arr[1]);
    const int32_t blkHeight  = atoi(arr[2].c_str());
    const int32_t blkChainId = atoi(arr[3].c_str());

    blkCache[blkHeight] = new RawBlock(atoi64(arr[0].c_str()), blkHeight, blkChainId, blkHash, arr[4].c_str());

    if (input.tellg() > lastOffset + maxReadSize) {
      lastOffset = input.tellg();
    }
  }
}

// 从文件读取raw block
void getRawBlockFromDisk(const int32_t height, string *rawHex,
                          int32_t *chainId, int64_t *blockId) {
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
  if (rawHex != nullptr)
    *rawHex  = it->second->hex_;
  if (chainId != nullptr)
    *chainId = it->second->chainId_;
  if (rawHex != nullptr)
    *blockId = it->second->blockId_;
}


////////////////////////////////////////////////////////////////////////////////
//-------------------------------- AddrHandler ---------------------------------
////////////////////////////////////////////////////////////////////////////////
AddrHandler::AddrHandler(const size_t addrCount, const string &filePreAddr) {
  addrInfo_.resize(addrCount);
  addrCount_ = addrCount;
  fwriter_ = fwriter;

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

void AddrHandler::dumpAddressAndTxs(map<int32_t, FILE *> &fAddrTxs,
                                    vector<FILE *> &fAddrs_) {
  const string now = date("%F %T");
  string s;

  for (auto it = addrInfo_.begin(); it != addrInfo_.end(); it++) {
//    // address tx
//    _saveAddrTx(it, fAddrTxs[tableIdx_AddrTxs(it->addrTx_.ymd_)], fwriter_);
//
//    // table.addresses_0000
//    //  `id`, `address`, `tx_count`, `total_received`, `total_sent`,
//    //  `unconfirmed_received`, `unconfirmed_sent`,`unconfirmed_tx_count`, `begin_tx_id`, `begin_tx_ymd`,
//    //  `end_tx_id`, `end_tx_ymd`, `last_confirmed_tx_id`,
//    //  `last_confirmed_tx_ymd`, `created_at`, `updated_at`
//    s = Strings::Format("%lld,%s,%lld,%lld,%lld,"
//                        "0,0,0,%lld,%d,%lld,%d,%lld,%d,%s,%s",
//                        it->addrId_, it->addrStr_, it->idx_, it->totalReceived_,
//                        it->totalSent_, it->beginTxId_, it->beginTxYmd_,
//                        it->endTxId_, it->endTxYmd_, it->endTxId_, it->endTxYmd_,
//                        now.c_str(), now.c_str());
//    fwriter_->append(s, fAddrs_[tableIdx_Addr(it->addrId_)]);
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

void TxHandler::addOutputs(const CTransaction &tx,
                           AddrHandler *addrHandler, const int32_t height,
                           map<string, int64_t> &addressBalance) {
  vector<struct TxInfo>::iterator it = find(tx.GetHash());
  it->outputs_ = (TxOutput **)calloc(tx.vout.size(), sizeof(TxOutput *));
  it->outputsCount_ = (int32_t)tx.vout.size();
  it->blockHeight_ = height;

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
    int i = -1;
    for (auto &addr : addresses) {
      i++;
      const string s = CBitcoinAddress(addr).ToString();
      ptr->address_.push_back(s);
      // 增加每个地址的余额
      addressBalance[s] += out.nValue;
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

void _saveTxOutput(TxInfo &txInfo, int32_t n) {
//  // table.tx_outputs_xxxx
//  //   `tx_id`,`position`,`address`,`address_ids`,`value`,`output_script_asm`,
//  //   `output_script_hex`,`output_script_type`,
//  //   `spent_tx_id`,`spent_position`,`created_at`,`updated_at`
//  string s;
//  const string now = date("%F %T");
//  TxOutput *poutput = *(txInfo.outputs_ + n);
//
//  // 处理地址
//  string addressStr, addressIdsStr;
//  for (int i = 0; i < poutput->address_.size(); i++) {
//    const string addrStr = poutput->address_[i];
//    addressStr    += Strings::Format("%s|", addrStr.c_str());
//    addressIdsStr += Strings::Format("%lld|", poutput->addressIds_[i]);
//  }
//  if (addressStr.length()) {  // 移除最后一个|
//    addressStr.resize(addressStr.length() - 1);
//    addressIdsStr.resize(addressIdsStr.length() - 1);
//  }
//
//  s = Strings::Format("%lld,%d,%s,%s,%lld,%s,%s,%s,%lld,%d,%s,%s",
//                      txInfo.txId_, n, addressStr.c_str(), addressIdsStr.c_str(),
//                      poutput->value_,
//                      poutput->scriptAsm_.c_str(), poutput->scriptHex_.c_str(),
//                      poutput->typeStr_.c_str(),
//                      poutput->spentTxId_, poutput->spentPosition_,
//                      now.c_str(), now.c_str());
//  fwriter->append(s, f);
}

void _saveUnspentOutput(TxInfo &txInfo, int32_t n,
                        vector<FILE *> &fUnspentOutputs,
                        FILE *fTxoutput) {
//  string s;
//  const string now = date("%F %T");
//  TxOutput *out = *(txInfo.outputs_ + n);
//  assert(out != nullptr);
//
//  // 遍历处理，可能含有多个地址
//  for (size_t i = 0; i < out->address_.size(); i++) {
//    // table.address_unspent_outputs_xxxx
//    // `address_id`, `tx_id`, `position`, `position2`, `block_height`,
//    // `value`, `output_script_type`, `created_at`
//    s = Strings::Format("%lld,%lld,%d,%d,%d,%lld,%s,%s",
//                        out->addressIds_[i], txInfo.txId_, n, i,
//                        txInfo.blockHeight_, out->value_,
//                        out->typeStr_.c_str(), now.c_str());
//    fwriter->append(s, fUnspentOutputs[tableIdx_AddrUnspentOutput(out->addressIds_[i])]);
//  }
//
//  // 也需要将未花费的，存入至table.tx_outputs_xxxx
//  _saveTxOutput(txInfo, n, fTxoutput, fwriter);
}

void TxHandler::dumpUnspentOutputToFile(vector<FILE *> &fUnspentOutputs,
                                        vector<FILE *> &fTxOutputs) {
//  // 遍历整个tx区，将未花费的数据写入文件
//  for (auto &it : txInfo_) {
//    if (it.outputs_ == nullptr) {
//      continue;
//    }
//    for (int32_t i = 0; i < it.outputsCount_; i++) {
//      if (*(it.outputs_ + i) == nullptr) {
//        continue;
//      }
//      _saveUnspentOutput(it, i, fUnspentOutputs,
//                         fTxOutputs[tableIdx_TxOutput(it.txId_)], fwriter_);
//    }
//    delOutputAll(it);
//  }
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

  //
  // open Rocks DB
  //
  // Optimize RocksDB. This is the easiest way to get RocksDB to perform well
  options_.IncreaseParallelism();
  options_.OptimizeLevelStyleCompaction();
  // create the DB if it's not already present
  options_.create_if_missing = true;

  const string kvpath = Strings::Format("./rocksdb_bootstrap_%d",
                                        (int32_t)Config::GConfig.getInt("raw.max.block.height", -1));
  rocksdb::Status s = rocksdb::DB::Open(options_, kvpath, &db_);
  if (!s.ok()) {
    THROW_EXCEPTION_DBEX("open rocks db fail");
  }
}

PreParser::~PreParser() {
  stop();
  delete db_;  // close kv db
}

void PreParser::stop() {
  if (running_) {
    running_ = false;
    LOG_INFO("stop PreParser...");
  }
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
  }
  {
    LogScope ls("init txs Handler");
    txHandler_   = new TxHandler(txCount_, filePreTx_);
  }
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
  fbb.Finish(blkBuilder.Finish());

  // 11_{block_hash}, 需紧接 blockBuilder.Finish()
  const string key11 = Strings::Format("%s%s", KVDB_PREFIX_BLOCK_OBJECT,
                                       b.blockHash_.ToString().c_str());
  kvSet(key11, fbb.GetBufferPointer(), fbb.GetSize());

  // 10_{block_height}
  const string key10 = Strings::Format("%s%010d", KVDB_PREFIX_BLOCK_HEIGHT, b.height_);
  kvSet(key10, b.blockHash_.ToString());
}

void PreParser::parseBlock(const CBlock &blk, const int64_t blockId,
                           const int32_t height, const int32_t blockBytes) {
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
      kvSet(key, value);
      value.clear();
    }
    i++;
  }

  if (value.size() != 0) {
    key = Strings::Format("%s%s_%d", KVDB_PREFIX_BLOCK_TXS_STR,
                          blk.GetHash().ToString().c_str(), (int32_t)(i/kBatchSize));
    kvSet(key, value);
    value.clear();
  }
}

void PreParser::parseTxInputs(const CTransaction &tx, const int64_t txId,
                              int64_t &valueIn,
                              map<string, int64_t> &addressBalance) {
//  int n;
//  vector<string> values;
//  const string now = date("%F %T");
//
//  n = -1;
//  for (auto &in : tx.vin) {
//    n++;
//
//    if (tx.IsCoinBase()) {
//      // 插入当前交易的inputs, coinbase tx的 scriptSig 不做decode，可能含有非法字符
//      // 通常无法解析成功。 coinbase无需担心其长度，bitcoind对coinbase tx的coinbase
//      // 字段长度做了限制
//      values.push_back(Strings::Format("%lld,%d,,%s,%u,"
//                                       "0,-1,0,,,%s",
//                                       txId, n,
//                                       HexStr(in.scriptSig.begin(), in.scriptSig.end()).c_str(),
//                                       in.nSequence, now.c_str()));
//    } else
//    {
//      uint256 prevHash = in.prevout.hash;
//      int64_t prevTxId = txHandler_->getTxId(prevHash);
//      int32_t prevPos  = (int32_t)in.prevout.n;
//
//      // 将前向交易标记为已花费
//      auto pTxInfo = txHandler_->find(prevHash);
//      TxOutput *poutput = *(pTxInfo->outputs_ + prevPos);
//      assert(poutput->spentTxId_ == 0);
//      assert(poutput->spentPosition_ == -1);
//      poutput->spentTxId_     = txId;
//      poutput->spentPosition_ = n;
//
//      // 保存前向交易output，一旦花掉，将不会再次使用
//      _saveTxOutput(*pTxInfo, prevPos,
//                    fTxOutputs_[tableIdx_TxOutput(pTxInfo->txId_)], fwriter_);
//
//      // 处理地址
//      string addressStr, addressIdsStr;
//      for (int i = 0; i < poutput->address_.size(); i++) {
//        const string addrStr = poutput->address_[i];
//        addressStr    += Strings::Format("%s|", addrStr.c_str());
//        addressIdsStr += Strings::Format("%lld|", poutput->addressIds_[i]);
//        // 减扣该地址额度
//        addressBalance[addrStr] += -1 * poutput->value_;
//      }
//      if (addressStr.length()) {  // 移除最后一个|
//        addressStr.resize(addressStr.length() - 1);
//        addressIdsStr.resize(addressIdsStr.length() - 1);
//      }
//
//      // 插入当前交易的inputs
//      values.push_back(Strings::Format("%lld,%d,%s,%s,%u,%lld,%d,"
//                                       "%lld,%s,%s,%s",
//                                       txId, n,
//                                       in.scriptSig.ToString().c_str(),
//                                       HexStr(in.scriptSig.begin(), in.scriptSig.end()).c_str(),
//                                       in.nSequence, prevTxId, prevPos,
//                                       poutput->value_,
//                                       addressStr.c_str(), addressIdsStr.c_str(),
//                                       now.c_str()));
//      valueIn += poutput->value_;
//
//      // 可以删除前向输入了，因每个前向输入只会使用一次
//      txHandler_->delOutput(prevHash, prevPos);
//    }
//  } /* /for */
//
//  // 保存inputs
//  for (auto &it : values) {
//    fwriter_->append(it, fTxInputs_[tableIdx_TxInput(txId)]);
//  }
}

void PreParser::parseTxSelf(const int32_t height, const int64_t txId, const uint256 &txHash,
                            const CTransaction &tx, const int64_t valueIn,
                            const int32_t ymd) {
  int64_t fee = 0;
  string s;
  const int64_t valueOut = tx.GetValueOut();
  if (tx.IsCoinBase()) {
    fee = valueOut - GetBlockValue(height, 0);  // coinbase的fee为 block rewards
  } else {
    fee = valueIn - valueOut;
  }

  // get tx bytes
  CDataStream ssTx(SER_NETWORK, BITCOIN_PROTOCOL_VERSION);
  ssTx << tx;
  const string txHex = HexStr(ssTx.begin(), ssTx.end());

//  // table.txs_xxxx
//  // `tx_id`, `hash`, `height`, `ymd`,`is_coinbase`,
//  // `version`, `lock_time`, `size`, `fee`, `total_in_value`,
//  // `total_out_value`, `inputs_count`, `outputs_count`, `created_at`
//  s = Strings::Format("%lld,%s,%d,%d,%d,%d,%u,%d,%lld,%lld,%lld,%d,%d,%s",
//                      txId, txHash.ToString().c_str(), height, ymd,
//                      tx.IsCoinBase() ? 1 : 0, tx.nVersion, tx.nLockTime,
//                      txHex.length()/2, fee, valueIn, valueOut,
//                      tx.vin.size(), tx.vout.size(),
//                      date("%F %T").c_str());



  // 写入s至磁盘
  fwriter_->append(s, fTxs_[tableIdx_Tx(txId)]);
}

void _saveAddrTx(vector<struct AddrInfo>::iterator addrInfo) {
//  string line;
//  AddrTx &t = addrInfo->addrTx_;
//
//  const string nowStr = date("%F %T");
//
//  // table.address_txs_<yyyymm>
//  // `address_id`, `tx_id`, `tx_height`, `total_received`, `balance_diff`,
//  // `balance_final`, `idx`, `ymd`, `prev_ymd`, `prev_tx_id`, `next_ymd`,
//  // `next_tx_id`, `created_at`, `updated_at`
//  line = Strings::Format("%lld,%lld,%d,%lld,%lld,%lld,%lld,"
//                         "%d,%d,%lld,%d,%lld,%s,%s",
//                         addrInfo->addrId_, t.txId_, t.txHeight_,
//                         addrInfo->totalReceived_, t.balanceDiff_, t.balanceFinal_,
//                         addrInfo->idx_, t.ymd_, t.prevYmd_, t.prevTxId_,
//                         t.nextYmd_, t.nextTxId_,
//                         nowStr.c_str(), nowStr.c_str());
//  fwriter->append(line, f);
}

void PreParser::handleAddressTxs(const map<string, int64_t> &addressBalance,
                                 const int64_t txId, const int32_t ymd, const int32_t height) {
  for (auto &it : addressBalance) {
    const string &addrStr      = it.first;
    const int64_t &balanceDiff = it.second;
    vector<struct AddrInfo>::iterator addrInfo = addrHandler_->find(addrStr);

    // 记录当前交易信息
    struct AddrTx cur;
    cur.txId_         = txId;
    cur.txHeight_     = height;
    cur.balanceDiff_  = balanceDiff;
    cur.balanceFinal_ = addrInfo->totalReceived_ - addrInfo->totalSent_ + balanceDiff;
    assert(cur.balanceFinal_ >= 0);
    cur.ymd_      = ymd;
    cur.nextYmd_  = 0;
    cur.nextTxId_ = 0;
    cur.prevTxId_ = 0;
    cur.prevYmd_  = 0;

    // 更新上次交易，如果有的话
    if (addrInfo->addrTx_.txId_ != 0) {
      cur.prevTxId_ = addrInfo->addrTx_.txId_;
      cur.prevYmd_  = addrInfo->addrTx_.ymd_;
      addrInfo->addrTx_.nextTxId_ = txId;
      addrInfo->addrTx_.nextYmd_  = ymd;

      // save last one
      assert(addrInfo->addrTx_.ymd_ != 0);
      _saveAddrTx(addrInfo,
                  fAddrTxs_[tableIdx_AddrTxs(addrInfo->addrTx_.ymd_)], fwriter_);
    }

    // 变更当前记录相关值
    if (addrInfo->beginTxId_ == 0) {
      addrInfo->beginTxId_  = txId;
      addrInfo->beginTxYmd_ = ymd;
    }
    addrInfo->endTxId_  = txId;
    addrInfo->endTxYmd_ = ymd;
    addrInfo->totalReceived_ += balanceDiff > 0 ? balanceDiff : 0;
    addrInfo->totalSent_     += balanceDiff < 0 ? balanceDiff * -1 : 0;
    addrInfo->idx_++;

    // switch
    memcpy(&(addrInfo->addrTx_), &cur, sizeof(struct AddrTx));
  }
}

void PreParser::parseTx(const int32_t height, const CTransaction &tx,
                        const uint32_t nTime) {
//  const uint256 txHash = tx.GetHash();
//  LOG_DEBUG("parse tx, height: %d, hash: %s", height, txHash.ToString().c_str());
//
//  // 硬编码特殊交易处理
//  //
//  // 1. tx hash: d5d27987d2a3dfc724e359870c6644b40e497bdc0589a033220fe15429d88599
//  // 该交易在两个不同的高度块(91812, 91842)中出现过
//  // 91842块中有且仅有这一个交易
//  //
//  // 2. tx hash: e3bf3d07d4b0375638d5f1db5255fe07ba2c4cb067cd81b84ee974b6585fb468
//  // 该交易在两个不同的高度块(91722, 91880)中出现过
//  if ((height == 91842 &&
//       txHash == uint256("d5d27987d2a3dfc724e359870c6644b40e497bdc0589a033220fe15429d88599")) ||
//      (height == 91880 &&
//       txHash == uint256("e3bf3d07d4b0375638d5f1db5255fe07ba2c4cb067cd81b84ee974b6585fb468"))) {
//    LOG_WARN("ignore tx, height: %d, hash: %s",
//             height, txHash.ToString().c_str());
//    return;
//  }
//
//  const int64_t txId = txHandler_->getTxId(txHash);
//  int64_t valueIn = 0;
//  map<string, int64_t> addressBalance;
//
//  // inputs
//  parseTxInputs(tx, txId, valueIn, addressBalance);
//
//  // ouputs
//  txHandler_->addOutputs(tx, addrHandler_, height, addressBalance);
//
//  // 根据修正时间存储
//  const int32_t ymd = atoi(date("%Y%m%d", blkTs_.getMaxTimestamp()).c_str());
//
//  // tx self
//  parseTxSelf(height, txId, txHash, tx, valueIn, ymd);
//
//  // 处理地址变更
//  handleAddressTxs(addressBalance, txId, ymd, height);
}

void PreParser::run() {
  // 解析
  while (running_) {
    if (curHeight_ > stopHeight_) {
      LOG_INFO("reach max height: %d", stopHeight_);
      break;
    }

    string blkRawHex;
    int32_t chainId;
    int64_t blockId;
    getRawBlockFromDisk(curHeight_, &blkRawHex, &chainId, &blockId);

    // 解码Raw Hex
    vector<unsigned char> blockData(ParseHex(blkRawHex));
    CDataStream ssBlock(blockData, SER_NETWORK, BITCOIN_PROTOCOL_VERSION);
    CBlock blk;
    try {
      ssBlock >> blk;
    }
    catch (std::exception &e) {
      THROW_EXCEPTION_DBEX("Block decode failed, height: %d, blockId: %lld",
                           curHeight_, blockId);
    }

    // 处理块
    LOG_INFO("parse block, height: %6d, txs: %5lld", curHeight_, blk.vtx.size());
    parseBlock(blk, blockId, curHeight_, (int32_t)blkRawHex.length()/2);

    // 处理交易
    for (auto &tx : blk.vtx) {
      parseTx(curHeight_, tx, blk.nTime);
    }

    curHeight_++;
  }

  if (running_) {
    // 清理数据：未花费的output
    LogScope ls("dump unspent output to file");
    txHandler_->dumpUnspentOutputToFile(fUnspentOutputs_, fTxOutputs_);
  }
  if (running_) {
    // 清理数据：地址数据, 地址最后关联的交易
    LogScope ls("dump address and txs");
    addrHandler_->dumpAddressAndTxs(fAddrTxs_, fAddrs_);
  }
}

