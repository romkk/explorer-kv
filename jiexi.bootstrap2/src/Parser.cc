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
static
void _saveAddrTx(vector<struct AddrInfo>::iterator addrInfo, FILE *f);


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

int64_t AddrHandler::getAddressId(const string &address) {
  return find(address)->addrId_;
}

void AddrHandler::dumpTxs(map<int32_t, FILE *> &fAddrTxs) {
  for (auto it = addrInfo_.begin(); it != addrInfo_.end(); it++) {
    _saveAddrTx(it, fAddrTxs[it->addrTx_.ymd_/100]);
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
    vector<string> arr = split(line, ',');

    txInfo_[i].hash256_ = uint256(arr[0]);
    txInfo_[i].txId_    = atoi64(arr[1].c_str());
    // blockHeight_ 尚未设置
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
  if (it->blockHeight_ == -1) { it->blockHeight_ = height; }

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

      // 增加每个地址的余额
      addressBalance[addressStr] += out.nValue;
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

void TxHandler::dumpUnspentOutputToFile(vector<FILE *> &fUnspentOutputs) {
  // TODO
  // 遍历整个tx区，将未花费的数据写入文件
  
}

int64_t TxHandler::getTxId(const uint256 &hash) {
  // TODO
  return 0ll;
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

  fBlocks_ = nullptr;

  memset(&blockInfo_, 0, sizeof(blockInfo_));
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

  // TODO
  // fBlocks_, fBlockTxs_

  while (running_) {
    sleep(1);
  }
}

void _saveBlock(BlockInfo &b, FILE *f) {
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
  fprintf(f, "%s\n", line.c_str());
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
  _saveBlock(blockInfo_, fBlocks_);
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
    fprintf(fBlockTxs_[blockId % 100], "%s\n", s.c_str());
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
      values.push_back(Strings::Format("%lld,%d,'','%s',%u,"
                                       "0,-1,0,'','','%s'",
                                       txId, n,
                                       HexStr(in.scriptSig.begin(), in.scriptSig.end()).c_str(),
                                       in.nSequence, now.c_str()));
    } else
    {
      uint256 prevHash = in.prevout.hash;
      int64_t prevTxId = txHandler_->getTxId(prevHash);
      int32_t prevPos  = (int32_t)in.prevout.n;

      // 将前向交易标记为已花费
      TxOutput *poutput = txHandler_->getOutput(prevHash, prevPos);
      assert(poutput->spentTxId_ == 0);
      assert(poutput->spentPosition_ == -1);
      poutput->spentTxId_     = txId;
      poutput->spentPosition_ = n;

      // TODO: 保存poutput磁盘

      // 处理地址
      string addressStr, addressIdsStr;
      for (int i = 0; i < poutput->address_.size(); i++) {
        const string addrStr = poutput->address_[i];
        addressStr    += Strings::Format("%s,", addrStr.c_str());
        addressIdsStr += Strings::Format("%lld,", poutput->addressIds_[i]);
        // 减扣该地址额度
        addressBalance[addrStr] += -1 * poutput->value_;
      }
      if (addressStr.length()) {  // 移除最后一个逗号
        addressStr.resize(addressStr.length() - 1);
        addressIdsStr.resize(addressIdsStr.length() - 1);
      }

      // 插入当前交易的inputs
      values.push_back(Strings::Format("%lld,%d,'%s','%s',%u,%lld,%d,"
                                       "%lld,'%s','%s','%s'",
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

  // TODO: 保存 inputs 至磁盘
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

  // TODO: 写入s至磁盘
}

void _saveAddrTx(vector<struct AddrInfo>::iterator addrInfo, FILE *f) {
  string line;
  AddrTx &t = addrInfo->addrTx_;

  // table.address_txs_<yyyymm>
  // `address_id`, `tx_id`, `tx_height`, `total_received`, `balance_diff`,
  // `balance_final`, `prev_ymd`, `prev_tx_id`, `next_ymd`, `next_tx_id`, `created_at`
  line = Strings::Format("%lld,%lld,%d,%lld,%lld,%lld,"
                         "%d,%lld,%d,%lld,%s",
                         addrInfo->addrId_, t.txId_, t.txHeight_, addrInfo->totalReceived_,
                         t.balanceDiff_, t.balanceFinal_, t.prevYmd_, t.prevTxId_,
                         t.nextYmd_, t.nextTxId_,
                         date("%F %T").c_str());
  fprintf(f, "%s\n", line.c_str());
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
      _saveAddrTx(addrInfo, fAddrTxs_[addrInfo->addrTx_.ymd_/100]);
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
    addrInfo->txCount_++;

    // switch
    memcpy(&(addrInfo->addrTx_), &cur, sizeof(struct AddrTx));
  }
}

void PreParser::parseTx(const int32_t height, const CTransaction &tx,
                        const uint32_t nTime) {
  const uint256 txHash = tx.GetHash();
  LOG_INFO("parse tx, height: %d, hash: %s", height, txHash.ToString().c_str());

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


void PreParser::parseBlocks() {
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
    parseBlock(blk, blockId, curHeight_, (int32_t)blkRawHex.length()/2, chainId);

    // 处理交易
    for (auto &tx : blk.vtx) {
      parseTx(curHeight_, tx, blk.nTime);
    }

    curHeight_++;
  }
}

// 完成清理工作
void PreParser::cleanup() {

}

void PreParser::run() {
  // 解析
  parseBlocks();

  // 最后清理数据：未花费的output, 地址最后关联的交易
  txHandler_->dumpUnspentOutputToFile(fUnspentOutputs_);
  addrHandler_->dumpTxs(fAddrTxs_);
}







