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

static void _getRawTxFromDisk(const uint256 &hash, const int32_t height,
                              string *hex, int64_t *txId);
static void _saveAddrTx(vector<struct AddrInfo>::iterator addrInfo, FILE *f);
static void _saveTxOutput(TxInfo &txInfo, int32_t n, FILE *f);

static FILE *openCSV(const char *fmt, ...) {
  // Strings::Format(const char * fmt, ...)
  char tmp[512];
  string dest;
  va_list al;
  va_start(al, fmt);
  int len = vsnprintf(tmp, 512, fmt, al);
  va_end(al);
  if (len>511) {
    char * destbuff = new char[len+1];
    va_start(al, fmt);
    len = vsnprintf(destbuff, len+1, fmt, al);
    va_end(al);
    dest.append(destbuff, len);
    delete destbuff;
  } else {
    dest.append(tmp, len);
  }

  const string dir = Config::GConfig.get("csv.data.dir");
  {
    // 目录不存在，尝试创建目录
    boost::filesystem::path datadir(dir.c_str());
    boost::filesystem::file_status s = boost::filesystem::status(datadir);
    if (!boost::filesystem::is_directory(s)) {
      LOG_INFO("mdkir: %s", dir.c_str());
      if (!boost::filesystem::create_directory(datadir)) {
        LOG_FATAL("mkdir failure: %s", dir.c_str());
      }
    }
  }

  string path = Strings::Format("%s/%s", dir.c_str(), dest.c_str());

  FILE *f = fopen(path.c_str(), "w");
  LOG_DEBUG("open file: %s", path.c_str());
  if (f == nullptr) {
    THROW_EXCEPTION_DBEX("open file failure: %s", dest.c_str());
  }
  return f;
}

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

  string path = Strings::Format("%d_%d", height2, height2 + (KCountPerFile - 1));

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
    map<int32_t, RawBlock*>().swap(blkCache);  // clear

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
AddrHandler::AddrHandler(const size_t addrCount, const string &filePreAddr,
                         FileWriter *fwriter) {
  addrInfo_.resize(addrCount);
  addrCount_ = addrCount;
  fwriter_ = fwriter;

  std::ifstream f(filePreAddr);
  std::string line;
  for (size_t i = 0; std::getline(f, line); ++i) {
    if (i > addrCount_) {
      THROW_EXCEPTION_DBEX("pre address count not match, i: %lld, addrCount_: %lld", i, addrCount_);
    }
    vector<string> arr = split(line, ',');

    addrInfo_[i].addrId_ = atoi64(arr[1]);
    strncpy(addrInfo_[i].addrStr_, arr[0].c_str(), arr[0].length());
  }
  // sort for binary search
  std::sort(addrInfo_.begin(), addrInfo_.end());
}

vector<struct AddrInfo>::iterator AddrHandler::find(const string &address) {
  AddrInfo needle;
  strncpy(needle.addrStr_, address.c_str(), 35);
  vector<struct AddrInfo>::iterator it;

  it = std::upper_bound(addrInfo_.begin(), addrInfo_.end(), needle);
  if (it > addrInfo_.end() || it <= addrInfo_.begin()) {
    THROW_EXCEPTION_DBEX("AddrHandler can't find AddrInfo by address: %s", address.c_str());
  }
  it--;
  if (strncmp(it->addrStr_, address.c_str(), 36) != 0) {
    THROW_EXCEPTION_DBEX("AddrHandler can't find AddrInfo by address: %s", address.c_str());
  }
  return it;
}

int64_t AddrHandler::getAddressId(const string &address) {
  return find(address)->addrId_;
}

void AddrHandler::dumpTxs(map<int32_t, FILE *> &fAddrTxs) {
  for (auto it = addrInfo_.begin(); it != addrInfo_.end(); it++) {
    _saveAddrTx(it, fAddrTxs[it->addrTx_.ymd_/100]);
  }
}

void AddrHandler::dumpAddresses(vector<FILE *> &fAddrs_) {
  string s;
  const string now = date("%F %T");
  for (auto &it : addrInfo_) {
    s = Strings::Format("%lld,%s,%lld,%lld,%lld,%lld,%d,%lld,%d,%s,%s",
                        it.addrId_, it.addrStr_, it.idx_, it.totalReceived_, it.totalSent_,
                        it.beginTxId_, it.beginTxYmd_,
                        it.endTxId_, it.endTxYmd_,
                        now.c_str(), now.c_str());
    fwriter_->append(s, fAddrs_[tableIdx_Addr(it.addrId_)]);
  }
}


////////////////////////////////////////////////////////////////////////////////
//--------------------------------- TxHandler ----------------------------------
////////////////////////////////////////////////////////////////////////////////
TxHandler::TxHandler(const size_t txCount, const string &file,
                     FileWriter *fwriter) {
  txInfo_.resize(txCount);
  txCount_ = txCount;
  fwriter_ = fwriter;

  std::ifstream f(file);
  std::string line;
  for (size_t i = 0; std::getline(f, line); ++i) {
    if (i > txCount_) {
      THROW_EXCEPTION_DBEX("pre tx count not match, i: %lld, txCount_: %lld", i, txCount_);
    }
    vector<string> arr = split(line, ',');

    txInfo_[i].hash256_ = uint256(arr[0]);
    txInfo_[i].txId_    = atoi64(arr[1].c_str());
    // blockHeight_ 尚未设置，后面设置output时会补上
  }
  // sort for binary search
  std::sort(txInfo_.begin(), txInfo_.end());
}

vector<struct TxInfo>::iterator TxHandler::find(const uint256 &hash) {
  TxInfo needle;
  needle.hash256_ = hash;
  vector<struct TxInfo>::iterator it;

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
      ptr->addressIds_.push_back(addrHandler->getAddressId(s));

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

void _saveTxOutput(TxInfo &txInfo, int32_t n, FILE *f, FileWriter *fwriter) {
  // table.tx_outputs_xxxx
  //   `tx_id`,`position`,`address`,`address_ids`,`value`,`output_script_asm`,
  //   `output_script_hex`,`output_script_type`,
  //   `spent_tx_id`,`spent_position`,`created_at`,`updated_at`
  string s;
  const string now = date("%F %T");
  TxOutput *poutput = *(txInfo.outputs_ + n);

  // 处理地址
  string addressStr, addressIdsStr;
  for (int i = 0; i < poutput->address_.size(); i++) {
    const string addrStr = poutput->address_[i];
    addressStr    += Strings::Format("%s|", addrStr.c_str());
    addressIdsStr += Strings::Format("%lld|", poutput->addressIds_[i]);
  }
  if (addressStr.length()) {  // 移除最后一个|
    addressStr.resize(addressStr.length() - 1);
    addressIdsStr.resize(addressIdsStr.length() - 1);
  }

  s = Strings::Format("%lld,%d,%s,%s,%lld,%s,%s,%s,%lld,%d,%s,%s",
                      txInfo.txId_, n, addressStr.c_str(), addressIdsStr.c_str(),
                      poutput->value_,
                      poutput->scriptAsm_.c_str(), poutput->scriptHex_.c_str(),
                      poutput->typeStr_.c_str(),
                      poutput->spentTxId_, poutput->spentPosition_,
                      now.c_str(), now.c_str());
  fwriter->append(s, f);
}

void _saveUnspentOutput(TxInfo &txInfo, int32_t n,
                        vector<FILE *> &fUnspentOutputs,
                        FILE *fTxoutput, FileWriter *fwriter) {
  string s;
  const string now = date("%F %T");
  TxOutput *out = *(txInfo.outputs_ + n);
  assert(out != nullptr);

  // 遍历处理，可能含有多个地址
  for (size_t i = 0; i < out->address_.size(); i++) {
    // table.address_unspent_outputs_xxxx
    // `address_id`, `tx_id`, `position`, `position2`, `block_height`,
    // `value`, `output_script_type`, `created_at`
    s = Strings::Format("%lld,%lld,%d,%d,%d,%lld,%s,%s",
                        out->addressIds_[i], txInfo.txId_, n, i,
                        txInfo.blockHeight_, out->value_,
                        out->typeStr_.c_str(), now.c_str());
    fwriter->append(s, fUnspentOutputs[tableIdx_AddrUnspentOutput(out->addressIds_[i])]);
  }

  // 也需要将未花费的，存入至table.tx_outputs_xxxx
  _saveTxOutput(txInfo, n, fTxoutput);
}

void TxHandler::dumpUnspentOutputToFile(vector<FILE *> &fUnspentOutputs,
                                        vector<FILE *> &fTxOutputs) {
  // 遍历整个tx区，将未花费的数据写入文件
  for (auto &it : txInfo_) {
    if (it.outputs_ == nullptr) {
      continue;
    }
    for (int32_t i = 0; i < it.outputsCount_; i++) {
      if (*(it.outputs_ + i) == nullptr) {
        continue;
      }
      _saveUnspentOutput(it, i, fUnspentOutputs,
                         fTxOutputs[tableIdx_TxOutput(it.txId_)]);
    }
    delOutputAll(it);
  }
}

int64_t TxHandler::getTxId(const uint256 &hash) {
  return find(hash)->txId_;
}



////////////////////////////////////////////////////////////////////////////////
//--------------------------------- FileWriter ---------------------------------
////////////////////////////////////////////////////////////////////////////////
FileWriter::FileWriter() {
  running_ = true;
  boost::thread t(boost::bind(&FileWriter::threadConsume, this));
}

FileWriter::~FileWriter() {
  stop();
  while (runningConsume_ == true) {
    sleepMs(10);
  }
}

void FileWriter::stop() {
  if (running_) {
    running_ = false;
    LOG_INFO("stop FileWriter...");
  }
}

void FileWriter::append(const string &s, FILE *f) {
  // buffer大小有限制，防止占用过大内存
  while (running_) {
    lock_.lock();
    if (buffer_.size() < 100 * 10000) {
      buffer_.push_back(make_pair(f, s));
      lock_.unlock();
      break;
    }
    lock_.unlock();
    sleepMs(100);
  }
}

void FileWriter::threadConsume() {
  LogScope ls("FileWriter::threadConsume");
  runningConsume_ = true;
  while (running_) {
    vector<std::pair<FILE *, string> > cache;
    {
      ScopeLock sl(lock_);
      while (buffer_.size() > 0) {
        cache.push_back(*buffer_.rbegin());
        buffer_.pop_back();
      }
    }

    if (cache.size() == 0) {
      sleepMs(50);
      continue;
    }
    for (auto &it : cache) {
      fprintf(it.first, "%s\n", it.second.c_str());
    }
  }
  runningConsume_ = false;
}



////////////////////////////////////////////////////////////////////////////////
//--------------------------------- PreParser ----------------------------------
////////////////////////////////////////////////////////////////////////////////
PreParser::PreParser() {
  stopHeight_  = (int32_t)Config::GConfig.getInt("raw.max.block.height", -1);
  filePreTx_   = Config::GConfig.get("pre.tx.output.file", "");
  filePreAddr_ = Config::GConfig.get("pre.address.output.file", "");
  txCount_ = addrCount_ = 0;
  addrHandler_ = nullptr;
  curHeight_  = 0;
  running_ = true;

  fwriter_ = new FileWriter();

  memset(&blockInfo_, 0, sizeof(blockInfo_));
}

PreParser::~PreParser() {
  stop();
  delete fwriter_;
  closeFiles();
}

void PreParser::stop() {
  if (running_) {
    running_ = false;
    LOG_INFO("stop PreParser...");
  }
  fwriter_->stop();
}

void PreParser::openFiles() {
  fBlocks_ = openCSV("0_blocks.csv");

  // table.block_txs_xxxx
  for (int i = 0; i < 100; i++) {
    fBlockTxs_.push_back(openCSV("block_txs_%04d.csv", i));
  }

  // table.txs_xxxx
  for (int i = 0; i < 64; i++) {
    fTxs_.push_back(openCSV("txs_%04d.csv", i));
  }

  // table.tx_inputs_xxxx
  for (int i = 0; i < 100; i++) {
    fTxInputs_.push_back(openCSV("tx_inputs_%04d.csv", i));
  }

  // table.tx_outputs_xxxx
  for (int i = 0; i < 100; i++) {
    fTxOutputs_.push_back(openCSV("tx_outputs_%04d.csv", i));
  }

  // table.address_unspent_outputs_xxxx
  for (int i = 0; i < 10; i++) {
    fUnspentOutputs_.push_back(openCSV("address_unspent_outputs_%04d.csv", i));
  }

  // table.addresses_xxxx
  for (int i = 0; i < 64; i++) {
    fAddrs_.push_back(openCSV("addresses_%04d.csv", i));
  }

  // table.address_txs_xxxx
  const uint32_t startTs = 1230963305u;  // block 0: 2009-01-03 18:15:05
  const uint32_t endTs   = (uint32_t)time(nullptr);
  for (uint32_t ts = startTs; ts <= endTs; ts += 86400) {
    const int32_t ymd = atoi(date("%Y%m", ts));
    if (fAddrTxs_.find(ymd) != fAddrTxs_.end()) {
      continue;
    }
    fAddrTxs_.insert(std::make_pair(ymd, openCSV("address_txs_%06d.csv", ymd)));
  }
}

void PreParser::closeFiles() {
  fclose(fBlocks_);
  for (auto &it : fBlockTxs_) {
    fclose(it);
  }
  for (auto &it : fTxs_) {
    fclose(it);
  }
  for (auto &it : fTxInputs_) {
    fclose(it);
  }
  for (auto &it : fTxOutputs_) {
    fclose(it);
  }
  for (auto &it : fUnspentOutputs_) {
    fclose(it);
  }
  for (auto &it : fAddrTxs_) {
    fclose(it.second);
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
    addrHandler_ = new AddrHandler(addrCount_, filePreAddr_, fwriter_);
  }
  {
    LogScope ls("init txs Handler");
    txHandler_   = new TxHandler(txCount_, filePreTx_, fwriter_);
  }

  // 初始化各类文件句柄
  openFiles();
}

void _saveBlock(BlockInfo &b, FILE *f, FileWriter *fwriter) {
  string line;
  // 保存当前Block, table.0_blocks, 字段顺序严格按照表顺序
  // `block_id`, `height`, `hash`, `version`, `mrkl_root`, `timestamp`,
  // `bits`, `nonce`, `prev_block_id`, `prev_block_hash`,
  // `next_block_id`, `next_block_hash`, `chain_id`, `size`,
  // `difficulty`, `tx_count`, `reward_block`, `reward_fees`, `created_at`
  line = Strings::Format("%lld,%d,%s,%d,%s,%u,%lld,%lld,"
                         "%lld,%s,%lld,%s,"
                         "%d,%d,%llu,%d,%lld,%lld,%s",
                         b.blockId_, b.height_, b.blockHash_.ToString().c_str(),
                         b.header_.nVersion, b.header_.hashMerkleRoot.ToString().c_str(),
                         (uint32_t)b.header_.nTime, b.header_.nBits, b.header_.nNonce,
                         b.prevBlockId_, b.header_.hashPrevBlock.ToString().c_str(),
                         b.nextBlockId_, b.nextBlockHash_.ToString().c_str(),
                         b.chainId_, b.size_, b.diff_, b.txCount_,
                         b.rewardBlock_, b.rewardFee_, date("%F %T").c_str());
  fwriter->append(line, f);
}

void PreParser::parseBlock(const CBlock &blk, const int64_t blockId,
                           const int32_t height, const int32_t blockBytes,
                           const int32_t chainId) {
  CBlockHeader header = blk.GetBlockHeader();  // alias

  BlockInfo cur;
  cur.blockId_   = blockId;
  cur.blockHash_ = blk.GetHash();
  cur.chainId_   = chainId;
  BitsToDifficulty(header.nBits, cur.diff_);
  cur.header_    = header;
  cur.height_    = height;
  cur.nextBlockHash_ = uint256();
  cur.nextBlockId_   = 0;
  cur.prevBlockId_   = 0;
  cur.size_  = blockBytes;
  cur.rewardBlock_ = GetBlockValue(height, 0);
  cur.rewardFee_   = blk.vtx[0].GetValueOut() - cur.rewardBlock_;
  cur.txCount_ = (int32_t)blk.vtx.size();

  if (height > 0) {
    blockInfo_.nextBlockId_   = cur.blockId_;
    blockInfo_.nextBlockHash_ = cur.blockHash_;
    cur.prevBlockId_ = blockInfo_.blockId_;
  }

  // 保存
  if (height > 0) {
    _saveBlock(blockInfo_, fBlocks_);
  }
  memcpy(&blockInfo_, &cur, sizeof(BlockInfo));

  // 保存最后一个
  if (height == stopHeight_) {
    _saveBlock(blockInfo_, fBlocks_);
  }

  // 保存当前块对应的交易
  // table.block_txs_xxxx: block_id, position, tx_id, created_at
  int i = 0;
  const string now = date("%F %T");
  for (auto & it : blk.vtx) {
    string s = Strings::Format("%lld,%d,%lld,%s", blockId, i++,
                               txHandler_->getTxId(it.GetHash()), now.c_str());
    fwriter_->append(s, fBlockTxs_[tableIdx_BlockTxs(blockId)]);
  }
}

void PreParser::parseTxInputs(const CTransaction &tx, const int64_t txId,
                              int64_t &valueIn,
                              map<string, int64_t> &addressBalance) {
  int n;
  vector<string> values;
  const string now = date("%F %T");

  n = -1;
  for (auto &in : tx.vin) {
    n++;

    if (tx.IsCoinBase()) {
      // 插入当前交易的inputs, coinbase tx的 scriptSig 不做decode，可能含有非法字符
      // 通常无法解析成功。 coinbase无需担心其长度，bitcoind对coinbase tx的coinbase
      // 字段长度做了限制
      values.push_back(Strings::Format("%lld,%d,,%s,%u,"
                                       "0,-1,0,,,%s",
                                       txId, n,
                                       HexStr(in.scriptSig.begin(), in.scriptSig.end()).c_str(),
                                       in.nSequence, now.c_str()));
    } else
    {
      uint256 prevHash = in.prevout.hash;
      int64_t prevTxId = txHandler_->getTxId(prevHash);
      int32_t prevPos  = (int32_t)in.prevout.n;

      // 将前向交易标记为已花费
      auto pTxInfo = txHandler_->find(prevHash);
      TxOutput *poutput = *(pTxInfo->outputs_ + prevPos);
      assert(poutput->spentTxId_ == 0);
      assert(poutput->spentPosition_ == -1);
      poutput->spentTxId_     = txId;
      poutput->spentPosition_ = n;

      // 保存前向交易output，一旦花掉，将不会再次使用
      _saveTxOutput(*pTxInfo, prevPos, fTxOutputs_[tableIdx_TxOutput(pTxInfo->txId_)]);

      // 处理地址
      string addressStr, addressIdsStr;
      for (int i = 0; i < poutput->address_.size(); i++) {
        const string addrStr = poutput->address_[i];
        addressStr    += Strings::Format("%s|", addrStr.c_str());
        addressIdsStr += Strings::Format("%lld|", poutput->addressIds_[i]);
        // 减扣该地址额度
        addressBalance[addrStr] += -1 * poutput->value_;
      }
      if (addressStr.length()) {  // 移除最后一个|
        addressStr.resize(addressStr.length() - 1);
        addressIdsStr.resize(addressIdsStr.length() - 1);
      }

      // 插入当前交易的inputs
      values.push_back(Strings::Format("%lld,%d,%s,%s,%u,%lld,%d,"
                                       "%lld,%s,%s,%s",
                                       txId, n,
                                       in.scriptSig.ToString().c_str(),
                                       HexStr(in.scriptSig.begin(), in.scriptSig.end()).c_str(),
                                       in.nSequence, prevTxId, prevPos,
                                       poutput->value_,
                                       addressStr.c_str(), addressIdsStr.c_str(),
                                       now.c_str()));
      valueIn += poutput->value_;

      // 可以删除前向输入了，因每个前向输入只会使用一次
      txHandler_->delOutput(prevHash, prevPos);
    }
  } /* /for */

  // 保存inputs
  for (auto &it : values) {
    fwriter_->append(it, fTxInputs_[tableIdx_TxInput(txId)]);
  }
}

void PreParser::parseTxSelf(const int32_t height, const int64_t txId, const uint256 &txHash,
                            const CTransaction &tx, const int64_t valueIn,
                            const uint32_t nTime) {
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

  // table.txs_xxxx
  // `tx_id`, `hash`, `height`, `block_timestamp`,`is_coinbase`,
  // `version`, `lock_time`, `size`, `fee`, `total_in_value`,
  // `total_out_value`, `inputs_count`, `outputs_count`, `created_at`
  s = Strings::Format("%lld,%s,%d,%u,%d,%d,%u,%d,%lld,%lld,%lld,%d,%d,%s",
                      txId, txHash.ToString().c_str(), height, nTime,
                      tx.IsCoinBase() ? 1 : 0, tx.nVersion, tx.nLockTime,
                      txHex.length()/2, fee, valueIn, valueOut,
                      tx.vin.size(), tx.vout.size(),
                      date("%F %T").c_str());

  // 写入s至磁盘
  fwriter_->append(s, fTxs_[tableIdx_Tx(txId)]);
}

void _saveAddrTx(vector<struct AddrInfo>::iterator addrInfo,
                 FILE *f, FileWriter *fwriter) {
  string line;
  AddrTx &t = addrInfo->addrTx_;

  // table.address_txs_<yyyymm>
  // `address_id`, `tx_id`, `tx_height`, `total_received`, `balance_diff`, `idx`,
  // `balance_final`, `prev_ymd`, `prev_tx_id`, `next_ymd`, `next_tx_id`, `created_at`
  line = Strings::Format("%lld,%lld,%d,%lld,%lld,%lld,%lld,"
                         "%d,%lld,%d,%lld,%s",
                         addrInfo->addrId_, t.txId_, t.txHeight_, addrInfo->idx_,
                         addrInfo->totalReceived_,
                         t.balanceDiff_, t.balanceFinal_, t.prevYmd_, t.prevTxId_,
                         t.nextYmd_, t.nextTxId_,
                         date("%F %T").c_str());
  fwriter->append(line, f);
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
      _saveAddrTx(addrInfo, fAddrTxs_[tableIdx_AddrTxs(addrInfo->addrTx_.ymd_)]);
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
  const uint256 txHash = tx.GetHash();
  LOG_DEBUG("parse tx, height: %d, hash: %s", height, txHash.ToString().c_str());

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
       txHash == uint256("e3bf3d07d4b0375638d5f1db5255fe07ba2c4cb067cd81b84ee974b6585fb468"))) {
    LOG_WARN("ignore tx, height: %d, hash: %s",
             height, txHash.ToString().c_str());
    return;
  }

  const int64_t txId = txHandler_->getTxId(txHash);
  int64_t valueIn = 0;
  map<string, int64_t> addressBalance;

  // inputs
  parseTxInputs(tx, txId, valueIn, addressBalance);

  // ouputs
  txHandler_->addOutputs(tx, addrHandler_, height, addressBalance);

  // tx self
  parseTxSelf(height, txId, txHash, tx, valueIn, nTime);

  // 处理地址变更
  const int32_t ymd = atoi(date("%Y%m%d", nTime).c_str());
  handleAddressTxs(addressBalance, txId, ymd, height);
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
    parseBlock(blk, blockId, curHeight_, (int32_t)blkRawHex.length()/2, chainId);

    // 处理交易
    for (auto &tx : blk.vtx) {
      parseTx(curHeight_, tx, blk.nTime);
    }

    curHeight_++;
  }

  if (running_) {
    // 清理数据：未花费的output
    txHandler_->dumpUnspentOutputToFile(fUnspentOutputs_, fTxOutputs_);
  }
  if (running_) {
    // 清理数据：地址最后关联的交易
    addrHandler_->dumpTxs(fAddrTxs_);
  }
  if (running_) {
    // 导入地址数据
    addrHandler_->dumpAddresses(fAddrs_);
  }
}







