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

#include "Log1Producer.h"

#include "BitcoinRpc.h"
#include "Common.h"
#include "Util.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>

#include <boost/filesystem.hpp>

namespace fs = boost::filesystem;


static JsonNode _bitcoindRpcRequest(BitcoinRpc &bitcoind, const string &request) {
  string response;
  const int ret = bitcoind.jsonCall(request, response, 5000/* timeout: ms */);
  if (ret != 0) {
    THROW_EXCEPTION_DBEX("bitcoind rpc call fail, req: %s", request.c_str());
  }
  JsonNode r;
  JsonNode::parse(response.c_str(), response.c_str() + response.length(), r);
  if (r["error"].type() != Utilities::JS::type::Undefined &&
      r["error"].type() == Utilities::JS::type::Obj) {
    THROW_EXCEPTION_DBEX("bitcoind rpc call fail, code: %d, error: %s",
                         r["error"]["code"].int32(),
                         r["error"]["message"].str().c_str());
  }
  return r["result"];
}

///////////////////////////////////  Log1  /////////////////////////////////////
Log1::Log1(): type_(-1), blockHeight_(-1) {
}

Log1::~Log1() {
}

// 解析当行 log1 类型的日志，解析失败则抛出异常
void Log1::parse(const string &line) {
  // 按照 ',' 切分，最多切三份
  const vector<string> arr1 = split(line, ',', 3);
  assert(arr1.size() == 3);

  // type
  const int32_t type = atoi(arr1[1].c_str());

  // 最后一个按照 '|' 切分
  const vector<string> arr2 = split(arr1[2], '|');

  /* block */
  if (type == TYPE_BLOCK) {
    assert(arr2.size() == 3);
    blockHeight_ = atoi(arr2[0].c_str());
    content_     = arr2[1] + arr2[2];
    type_        = type;
  }
  /* tx */
  else if (type == TYPE_TX) {
    assert(arr2.size() == 2);
    content_ = arr2[0] + arr2[1];
    type_    = type;
  } else {
    THROW_EXCEPTION_DBEX("[Log1::parse] invalid log1 type(%d)", type);
  }
}

const CBlock &Log1::getBlock() {
  if (block_.IsNull()) {  // 延时解析
    const string hex = content_.substr(64);
    if (!DecodeHexBlk(block_, hex)) {
      THROW_EXCEPTION_DBEX("decode block failure, hex: %s", content_.c_str());
    }
    if (block_.GetHash().ToString() != content_.substr(0, 64)) {
      THROW_EXCEPTION_DBEX("decode block failure, hash not match, hex: %s", content_.c_str());
    }
  }
  return block_;
}

const CTransaction &Log1::getTx() {
  if (tx_.IsNull()) {  // 延时解析
    const string hex = content_.substr(64);
    if (!DecodeHexTx(tx_, hex)) {
      THROW_EXCEPTION_DBEX("decode tx failure, hex: %s", content_.c_str());
    }
    if (tx_.GetHash().ToString() != content_.substr(0, 64)) {
      THROW_EXCEPTION_DBEX("decode tx failure, hash not match, hex: %s", content_.c_str());
    }
  }
  return tx_;
}

bool Log1::isTx() {
  return type_ == TYPE_TX ? true : false;
}

bool Log1::isBlock() {
  return type_ == TYPE_BLOCK ? true : false;
}

//////////////////////////////////  Chain  /////////////////////////////////////

Chain::Chain(const int32_t limit): limit_(limit)
{
}
Chain::~Chain() {}

int32_t Chain::getCurHeight() const {
  if (blocks_.size() == 0) {
    return -1;
  }
  auto it = blocks_.rbegin();
  return it->first;
}

uint256 Chain::getCurHash() const {
  if (blocks_.size() == 0) {
    return uint256();
  }
  auto it = blocks_.rbegin();
  return it->second;
}

void Chain::pushFirst(const int32_t height, const uint256 &hash) {
  if (blocks_.size() != 0) {
    THROW_EXCEPTION_DBEX("blocks_ is not empty, size: %llu", blocks_.size());
  }
  blocks_[height] = hash;
  LOG_INFO("accept block, height: %d, hash: %s", height, hash.ToString().c_str());
}

void Chain::push(const int32_t height, const uint256 &hash,
                 const uint256 &prevHash) {
  const int32_t curHeight = getCurHeight();
  const uint256 curHash   = getCurHash();

  /********************* 前进 *********************/
  if (height == curHeight + 1) {
    if (prevHash != curHash) {
      THROW_EXCEPTION_DBEX("prev hash not match curHash, cur: %s, prev: %s",
                           curHash.ToString().c_str(),
                           prevHash.ToString().c_str());
    }
    blocks_[height] = hash;
    LOG_INFO("accept block, height: %d, hash: %s", height, hash.ToString().c_str());
  }
  /********************* 后退 *********************/
  else if (height + 1 == curHeight) {
    if (blocks_.size() <= 3) {
      THROW_EXCEPTION_DBEX("blocks should more than 3");
    }
    // 倒数第三个的hash应该是目前后退的prev hash
    auto it = blocks_.rbegin() + 2;
    if (prevHash != it->second) {
      THROW_EXCEPTION_DBEX("prev hash not match -3 block hash, -3: %s, prev: %s",
                           it->second.ToString().c_str(),
                           prevHash.ToString().c_str());
    }
    // 移除最后一个块
    blocks_.erase(std::prev(blocks_.end()));
    LOG_INFO("rollback block, height: %d, hash: %s", height, hash.ToString().c_str());
  }
  /********************* 异常 *********************/
  else {
    THROW_EXCEPTION_DBEX("invalid block height: %d, cur: %d", height, curHeight);
  }

  // 检查数量限制，超出后移除首个元素
  while (blocks_.size() > limit_) {
    blocks_.erase(blocks_.begin());
  }
}

size_t Chain::size() const {
  return blocks_.size();
}

void Chain::pop() {
  if (blocks_.size() == 0) { return; }
  // 移除最后一个块
  blocks_.erase(std::prev(blocks_.end()));
}

///////////////////////////////  Log1Producer  /////////////////////////////////
Log1Producer::Log1Producer() : log1LockFd_(-1), log1FileHandler_(nullptr),
  log1FileIndex_(-1), chain_(1000/* max blocks */),
  log0FileIndex_(-1), log0FileOffset_(-1)
{
  log1Dir_ = Config::GConfig.get("log1.dir");
  log0Dir_ = Config::GConfig.get("log0.dir");
}

Log1Producer::~Log1Producer() {
  if (log1LockFd_ != -1) {
    flock(log1LockFd_, LOCK_UN);
    close(log1LockFd_);
    log1LockFd_ = -1;
  }

  if (log1FileHandler_ != nullptr) {
    fsync(fileno(log1FileHandler_));
    fclose(log1FileHandler_);
    log1FileHandler_ = nullptr;
  }
}

void Log1Producer::stop() {
  running_ = false;
}

//
// 执行顺序是特定的，如需调整请谨慎考虑。初始化中有故障，均会抛出异常，中断程序
//
void Log1Producer::init() {
  //
  // 1. 初始化 log1
  //
  initLog1();

  //
  // 2. 与 bitcoind 同步
  //
  syncBitcoind();

  //
  // 3. 与 log0 同步 (同步即初试化)
  //
  syncLog0();

  running_ = true;
}

// 初始化 log1
void Log1Producer::initLog1() {
  LogScope ls("Log1Producer::initLog1()");

  // 加锁LOCK
  {
    const string lockFile = Strings::Format("%s/LOCK", log1Dir_.c_str());
    log1LockFd_ = open(lockFile.c_str(), O_RDWR|O_CREAT|O_TRUNC, 0644);
    if (flock(log1LockFd_, LOCK_EX) != 0) {
      LOG_FATAL("can't lock file: %s", lockFile.c_str());
    }
  }

  // 遍历Log1，找出最近的文件，和最近的记录
  std::set<int32_t> filesIdxs;  // log1所有文件
  const string filesDir = Strings::Format("%s/files", log1Dir_.c_str());
  fs::path filesPath(filesDir);
  tryCreateDirectory(filesPath);
  {
    int log1FileIndex = -1;
    for (fs::directory_iterator end, it(filesPath); it != end; ++it) {
      const int idx = atoi(it->path().stem().c_str());
      filesIdxs.insert(idx);

      if (idx > log1FileIndex) {
        log1FileIndex = idx;
      }
    }
    log1FileIndex_ = log1FileIndex;
  }

  // 找到最后的块高度 & 哈希
  if (log1FileIndex_ == -1) {
    // 没有log1文件，则采用配置文件的参数作为其实块信息
    chain_.pushFirst((int32_t)Config::GConfig.getInt("log1.begin.block.height"),
                     uint256(Config::GConfig.get("log1.begin.block.hash")));
  } else {
    // 利用set自动排序，从小向大遍历所有文件，重新载入块链
    // TODO: 性能优化，少读取一些log1日志文件
    for (auto fileIdx : filesIdxs) {
      ifstream fin(Strings::Format("%s/files/%d.log", log1Dir_.c_str(), fileIdx));
      string line;
      Log1 log1Item;
      while (getline(fin, line)) {
        log1Item.parse(line);
        if (log1Item.isTx()) { continue; }
        assert(log1Item.isBlock());
        chain_.push(log1Item.blockHeight_, log1Item.getBlock().GetHash(),
                    log1Item.getBlock().hashPrevBlock);
      }
    } /* /for */
  }
  assert(chain_.size() >= 1);

  LOG_INFO("log1 begin block: %d, %s", chain_.getCurHeight(),
           chain_.getCurHash().ToString().c_str());
}

static string _bitcoind_getBlockHashByHeight(BitcoinRpc &bitcoind, const int32_t height) {
  const string request = Strings::Format("{\"id\":1,\"method\":\"getblockhash\",\"params\":[%d]}",
                                         height);
  JsonNode res = _bitcoindRpcRequest(bitcoind, request);
  return res.str();
}

static string _bitcoind_getInfo(BitcoinRpc &bitcoind) {
  const string request = Strings::Format("{\"id\":1,\"method\":\"getinfo\",\"params\":[]}");
  JsonNode res = _bitcoindRpcRequest(bitcoind, request);
  return res.str();
}

static void _bitcoind_getBlockByHash(BitcoinRpc &bitcoind, const string &hashStr, CBlock &block) {
  const string request = Strings::Format("{\"id\":1,\"method\":\"getblock\",\"params\":[\"%s\", false]}",
                                         hashStr.c_str());
  JsonNode res = _bitcoindRpcRequest(bitcoind, request);
  const string hexStr = res.str();
  if (!DecodeHexBlk(block, hexStr)) {
    THROW_EXCEPTION_DBEX("decode block failure, hex: %s", hexStr.c_str());
  }
}

void Log1Producer::syncBitcoind() {
  LogScope ls("Log1Producer::syncBitcoind()");
  //
  // 假设bitcoind在我们同步的过程中，是不会发生剧烈分叉的（剧烈分叉是指在向前追的那条链发生
  // 迁移了，导致当前追的链失效）。如果发生剧烈分叉则导致异常退出，再次启动则会首先回退再跟
  // 进，依然可以同步上去。
  //
  // 第一步，先尝试找到高度和哈希一致的块，若log1最前面的不符合，则回退直至找到一致的块
  // 第二步，从一致块高度开始，每次加一，向前追，直至与bitcoind高度一致
  //
  BitcoinRpc bitcoind(Config::GConfig.get("bitcoind.uri"));

  //
  // 第一步，先尝试找到高度和哈希一致的块，若log1最前面的不符合，则回退直至找到一致的块
  //
  assert(chain_.size() >= 1);
  while (1) {
    // 检测最后一个块(即chain_的当前块)是否一致
    const string hashStr = _bitcoind_getBlockHashByHeight(bitcoind, chain_.getCurHeight());
    if (chain_.getCurHash().ToString() == hashStr) {
      LOG_INFO("found the same block, height: %d, hash: %s",
               chain_.getCurHeight(), chain_.getCurHash().ToString().c_str());
      break;
    }

    // 不一致，弹出最后一个块
    chain_.pop();
    LOG_INFO("chain pop block, height: %d, hash: %s",
             chain_.getCurHeight(), chain_.getCurHash().ToString().c_str());
    if (chain_.size() == 0) {
      THROW_EXCEPTION_DBEX("can't find matched block, bitcoind has a big fork");
    }
  }

  //
  // 第二步，从一致块高度开始，每次加一，向前追，直至与bitcoind高度一致
  //
  int32_t bitcoindBestHeight = -1;
  {
    const string request = Strings::Format("{\"id\":1,\"method\":\"getinfo\",\"params\":[]}");
    JsonNode res = _bitcoindRpcRequest(bitcoind, request);
    bitcoindBestHeight = res["blocks"].int32();
  }
  assert(bitcoindBestHeight > 0);

  while (chain_.getCurHeight() < bitcoindBestHeight) {
    const int32_t height = chain_.getCurHeight() + 1;
    const string hashStr = _bitcoind_getBlockHashByHeight(bitcoind, chain_.getCurHeight() + 1);

    CBlock block;
    _bitcoind_getBlockByHash(bitcoind, hashStr, block);
    assert(block.GetHash().ToString() == hashStr);

    chain_.push(height, block.GetHash(), block.hashPrevBlock);
    writeLog1Block(height, block);
    LOG_INFO("sync bitcoind block, height: %d, hash: %s", height, hashStr.c_str());
  }
}

void Log1Producer::syncLog0() {
  LogScope ls("Log1Producer::syncLog0()");
  bool syncSuccess = false;

  //
  // 遍历 log0 所有文件，直至找到一样的块，若找到则同步完成
  //
  std::set<int32_t> filesIdxs;  // log0 所有文件
  fs::path filesPath(Strings::Format("%s/files", log0Dir_.c_str()));
  tryCreateDirectory(filesPath);
  for (fs::directory_iterator end, it(filesPath); it != end; ++it) {
    filesIdxs.insert(atoi(it->path().stem().c_str()));
  }

  // 反序遍历，从最新的文件开始找
  for (auto it = filesIdxs.rbegin(); it != filesIdxs.rend(); it++) {
    ifstream fin(Strings::Format("%s/files/%d.log", log0Dir_.c_str(), *it));
    string line;
    Log1 log0Item;  // log0 里记录的也是log1格式
    while (getline(fin, line)) {
      log0Item.parse(line);
      if (log0Item.isTx()) { continue; }
      assert(log0Item.isBlock());
      if (log0Item.blockHeight_         != chain_.getCurHeight() ||
          log0Item.getBlock().GetHash() != chain_.getCurHash()) {
        continue;
      }
      // 找到高度和哈希一致的块
      log0FileIndex_  = *it;
      log0FileOffset_ = fin.tellg();
      LOG_INFO("sync log0 success, file idx: %d, offset: %lld",
               log0FileIndex_, log0FileOffset_);
      syncSuccess = true;
      break;
    } /* /while */

    if (syncSuccess) { break; }
  } /* /for */

  if (!syncSuccess) {
    THROW_EXCEPTION_DBEX("sync log0 failure");
  }
}

void Log1Producer::tryRemoveOldLog0() {
  const int32_t keepLogNum = (int32_t)Config::GConfig.getInt("log0.files.max.num", 24 * 3);
  int32_t fileIdx = log0FileIndex_ - keepLogNum;

  // 遍历，删除所有小序列号的文件
  while (fileIdx >= 0) {
    const string file = Strings::Format("%s/files/%d.log",
                                        log0Dir_.c_str(), fileIdx--);
    if (!fs::exists(fs::path(file))) {
      break;
    }
    // try delete
    LOG_INFO("remove old log0: %s", file.c_str());
    if (!fs::remove(fs::path(file))) {
      THROW_EXCEPTION_DBEX("remove old log0 failure: %s", file.c_str());
    }
  }
}

// 尝试从 log0 中读取 N 行日志
void Log1Producer::tryReadLog0(vector<string> &lines) {
  const string currFile = Strings::Format("%s/files/%d.log",
                                          log0Dir_.c_str(), log0FileIndex_);
  const string nextFile = Strings::Format("%s/files/%d.log",
                                          log0Dir_.c_str(), log0FileIndex_ + 1);

  // 判断是否存在下一个文件，需要在读取当前文件之间判断，防止读取漏掉现有文件的最后内容
  const bool isNextExist = fs::exists(fs::path(nextFile));

  //
  // 打开文件并尝试读取新行
  //
  ifstream log0Ifstream(currFile);
  if (!log0Ifstream.is_open()) {
    THROW_EXCEPTION_DBEX("open file failure: %s", currFile.c_str());
  }
  log0Ifstream.seekg(log0FileOffset_);
  string line;
  while (getline(log0Ifstream, line)) {  // getline()读不到内容，则会关闭 ifstream
    lines.push_back(line);
    log0FileOffset_ = log0Ifstream.tellg();

    if (lines.size() > 500) {  // 每次最多处理500条日志
      LOG_WARN("reach max limit, stop load log0 items");
      break;
    }
  }
  if (lines.size() > 0) {
    return;
  }

  //
  // 探测新文件，仅当前面没有读取到新内容的时候
  //
  if (isNextExist == true && lines.size() == 0) {
    // 存在新的文件，切换索引，重置offset
    log0FileIndex_++;
    log0FileOffset_ = 0;
    LOG_INFO("swith log0 file, old: %s, new: %s ", currFile.c_str(), nextFile.c_str());

    tryRemoveOldLog0();
  }
}

void Log1Producer::run() {
  LogScope ls("Log1Producer::run()");

  while (running_) {
    vector<string> lines;
    tryReadLog0(lines);

    if (!running_) { break; }
    if (lines.size() == 0) {
      sleep(1);
      continue;
    }

    for (const auto &line : lines) {
      Log1 log0Item;
      log0Item.parse(line);
      //
      // Tx
      //
      if (log0Item.isTx()) {
        // 交易的容错是最强的，即使当前块已经错乱了，推入交易仍然不会出问题
        writeLog1Tx(log0Item.getTx());
      }
      //
      // Block
      //
      else if (log0Item.isBlock()) {
        // 推入当前链中，如果块的前后关联有问题，则会抛出异常中断程序
        chain_.push(log0Item.blockHeight_,
                    log0Item.getBlock().GetHash(), log0Item.getBlock().hashPrevBlock);
        writeLog1Block(log0Item.blockHeight_, log0Item.getBlock());
      } else {
        THROW_EXCEPTION_DBEX("invalid log0 type, log line: %s", line.c_str());
      }
    } /* /for */
  } /* /while */
}

void Log1Producer::writeLog1(const int32_t type, const string &line) {
  //
  // 写 log1 日志
  //
  const string file = Strings::Format("%s/files/%d.log", log1Dir_.c_str(), log1FileIndex_);
  if (log1FileHandler_ == nullptr) {
    log1FileHandler_ = fopen(file.c_str(), "a");  // append mode
  }
  if (log1FileHandler_ == nullptr) {
    THROW_EXCEPTION_DBEX("open file failure: %s", file.c_str());
  }
  const string logLine = Strings::Format("%s,%d,%s\n",
                                         date("%F %T").c_str(),
                                         type, line.c_str());
  size_t res = fwrite(logLine.c_str(), 1U, logLine.length(), log1FileHandler_);
  if (res != logLine.length()) {
    THROW_EXCEPTION_DBEX("fwrite return size_t(%llu) is NOT match line length: %llu, file: %s",
                         res, logLine.length(), file.c_str());
  }
  fflush(log1FileHandler_);

  //
  // 切换 log1 日志：超过最大文件长度则关闭文件，下次写入时会自动打开新的文件
  //
  const int64_t log1FileMaxSize = Config::GConfig.getInt("log1.file.max.size",
                                                         50 * 1024 * 1024);
  fpos_t position = -1;
  if (fgetpos(log1FileHandler_, &position) == -1) {
    THROW_EXCEPTION_DBEX("fgetpos failure: %s", file.c_str());
  }
  if (position > log1FileMaxSize) {
    fsync(fileno(log1FileHandler_));
    fclose(log1FileHandler_);
    log1FileHandler_ = nullptr;
    log1FileIndex_++;
    LOG_INFO("log1's size(%lld) reach max(%lld), switch to new file index: %d",
             position, log1FileMaxSize, log1FileIndex_);
  }
}

void Log1Producer::writeLog1Tx(const CTransaction &tx) {
  const string hex = EncodeHexTx(tx);
  writeLog1(Log1::TYPE_TX,
            Strings::Format("%s|%s", tx.GetHash().ToString().c_str(), hex.c_str()));
}

void Log1Producer::writeLog1Block(const int32_t height, const CBlock &block) {
  CDataStream ssBlock(SER_NETWORK, BITCOIN_PROTOCOL_VERSION);
  ssBlock << block;
  std::string strHex = HexStr(ssBlock.begin(), ssBlock.end());
  writeLog1(Log1::TYPE_BLOCK,
            Strings::Format("%d|%s|%s", height,
                            block.GetHash().ToString().c_str(), strHex.c_str()));
}
