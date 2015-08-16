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
#include "util.h"
#include "Common.h"

#include <fcntl.h>

#include <boost/filesystem.hpp>

const CBlock &Log1::getBlock() const {
  return block_;
}

const CTransaction &Log1::getTx() const {
  return tx_;
}

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


Log1Producer::Log1Producer() : log1LockFd_(-1), log1FileIndex_(-1), chain_(1000)
{
}

Log1Producer::~Log1Producer() {
  if (log1LockFd_ != -1) {
    flock(log1LockFd_, LOCK_UN);
    close(log1LockFd_);
    log1LockFd_ = -1;
  }
}

// 初始化 log1
void Log1Producer::initLog1() {
  LogScope ls("Log1Producer::initLog1()");
  namespace fs = boost::filesystem;

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
    log1BlkBeginHeight_ = (int32_t)Config::GConfig.getInt("log1.begin.block.height");
    log1BlkBeginHash_   = uint256(Config::GConfig.get("log1.begin.block.hash"));
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

    log1BlkBeginHeight_ = chain_.getCurHeight();
    log1BlkBeginHash_   = chain_.getCurHash();
  }

  LOG_INFO("log1 begin block: %d, %s", log1BlkBeginHeight_,
           log1BlkBeginHash_.ToString().c_str());
}

void Log1Producer::syncBitcoind() {
  LogScope ls("Log1Producer::syncBitcoind()");
  namespace fs = boost::filesystem;
  //
  // 假设bitcoind在我们同步的过程中，是不会发生剧烈分叉的（剧烈分叉是指在向前追的那条链发生
  // 迁移了，导致当前追的链失效）。如果发生剧烈分叉则导致异常退出，再次启动则会首先回退再跟
  // 进，依然可以同步上去。
  //
  // 第一步，先尝试找到高度和哈希一致的块，若log1最前面的不符合，则回退直至找到一致的块
  // 第二步，从一致块高度开始，每次加一，向前追，直至与bitcoind高度一致
  //

}

void Log1Producer::syncLog0() {
  LogScope ls("Log1Producer::syncLog0()");
  namespace fs = boost::filesystem;
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
      if (log0Item.blockHeight_         != log1BlkBeginHeight_ ||
          log0Item.getBlock().GetHash() != log1BlkBeginHash_) {
        continue;
      }
      // 找到高度和哈希一致的块
      log0FileIndex_  = *it;
      log0FileOffset_ = fin.tellg();
      LOG_INFO("sync log0 success, idx: %d, offset: %lld",
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

// 尝试从 log0 中读取 N 行日志
void Log1Producer::tryReadLog0(Log1 &log0Item, vector<string> &lines) {
  namespace fs = boost::filesystem;

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
  }
}

void Log1Producer::run() {
  Log1 log0Item;
}

