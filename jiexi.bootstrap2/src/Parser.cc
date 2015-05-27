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

#include <algorithm>
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
AddrHandler::AddrHandler(const size_t addrCount, const string &filePreAddr) {
  addrInfo_.resize(addrCount);
  addrCount_ = addrCount;

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

const int64_t AddrHandler::getAddressId(const string &address) {
  return find(address)->addrId_;
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
    vector<string> arr = split(line, ',');

    txInfo_[i].hash256_ = uint256(arr[0]);
    txInfo_[i].txId_    = atoi64(arr[1].c_str());
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

void TxHandler::addOutputs(CTransaction &tx, AddrHandler *addrHandler) {
  vector<struct TxInfo>::iterator it = find(tx.GetHash());
  it->outputs_ = (TxOutput **)calloc(tx.vout.size(), sizeof(TxOutput *));
  it->outputsCount_ = (int32_t)tx.vout.size();

  int32_t n = -1;
  for (auto &out : tx.vout) {
    n++;
    TxOutput *ptr = new TxOutput();
    *(it->outputs_ + n) = ptr;

    // script
    ptr->scriptHex_ = HexStr(out.scriptPubKey.begin(), out.scriptPubKey.end());
    ptr->scriptAsm_ = out.scriptPubKey.ToString();
    // asm大小超过1MB, 且大于hex的4倍，则认为asm是非法的，置空
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
      LOG_WARN("extract destinations failure, txId: %lld, hash: %s, position: %d",
               tx.GetHash().ToString().c_str(), n);
    }

    // type
    ptr->typeStr_ = GetTxnOutputType(type) ? GetTxnOutputType(type) : "";

    // address, address_ids
    int i = -1;
    for (auto &addr : addresses) {
      i++;
      const string addrStr = CBitcoinAddress(addr).ToString();
      ptr->address_.push_back(addrStr);
      ptr->addressIds_.push_back(addrHandler->getAddressId(addrStr));
    }
  }
}

void TxHandler::delOutput(const uint256 &hash, const int32_t n) {
  auto it = find(hash);
  if (it->outputs_ == nullptr || *(it->outputs_ + n) == nullptr) {
    THROW_EXCEPTION_DBEX("already delete output: %s,%d",
                         hash.ToString().c_str(), n);
  }
  delete *(it->outputs_ + n);
  *(it->outputs_ + n) = nullptr;

  // 检测是否释放整个tx的output部分. 很多tx的所有输出是花掉的状态，free之尽量回收内存
  bool isEmpty = true;
  for (int i = 0; i < it->outputsCount_; i++) {
    if (*(it->outputs_ + i) != nullptr) {
      isEmpty = false;
      break;
    }
  }
  if (isEmpty) {
    free(it->outputs_);
    it->outputs_ = nullptr;
  }
}

class TxOutput *TxHandler::getOutput(const uint256 &hash, const int32_t n) {
  auto it = find(hash);
  if (it->outputs_ == nullptr || *(it->outputs_ + n) == nullptr) {
    THROW_EXCEPTION_DBEX("can't get output: %s,%d", hash.ToString().c_str(), n);
  }
  return *(it->outputs_ + n);
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
  height_  = 0;
  running_ = true;
}

PreParser::~PreParser() {
  stop();
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


  while (running_) {
    sleep(1);
  }
}
