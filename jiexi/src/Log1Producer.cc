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

#include <iostream>
#include <fstream>

#include <fcntl.h>

#include <boost/filesystem.hpp>


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
  const string filesDir = Strings::Format("%s/files", log1Dir_.c_str());
  fs::path filesPath(filesDir);
  tryCreateDirectory(filesPath);
  {
    int log1FileIndex = -1;
    for (fs::directory_iterator end, it(filesPath); it != end; ++it) {
      const int idx = atoi(it->path().stem().c_str());
      if (idx > log1FileIndex) {
        log1FileIndex = idx;
      }
    }
    log1FileIndex_  = log1FileIndex;
  }

  // 找到最后的块高度 & 哈希
  if (log1FileIndex_ == -1) {
    // 没有log1文件，则采用配置文件的参数作为其实块信息
    log1BlkHeight_  = (int32_t)Config::GConfig.getInt("log1.begin.block.height");
    log1BlkHash_    = uint256(Config::GConfig.get("log1.begin.block.hash"));
    log1FileOffset_ = 0;
  } else {
    // 存在log1文件，找最后一条块记录
    ifstream fin(Strings::Format("%s/files/%d.log", log1Dir_.c_str(), log1FileIndex_));
    string line;
    Log1 log1Item;
    while (getline(fin, line)) {
      log1Item.parse(line);
      if (log1Item.isTx()) { continue; }
      assert(log1Item.isBlock());
      log1BlkHash_   = log1Item.block_.GetHash();
      log1BlkHeight_ = log1Item.blockHeight_;
    }
  }

  // 载入最近块链: 遍历log1的文件，慢慢载入
  {
    // 遍历log1所有文件，存入set
    std::set<int32_t> idxs;
    for (fs::directory_iterator end, it(filesPath); it != end; ++it) {
      const int idx = atoi(it->path().stem().c_str());
      idxs.insert(idx);
    }

    // 利用set自动排序，下一步会从小向大，遍历所有文件，重新载入块链
    // TODO: 性能优化，少读取一些log1日志文件
    for (auto fileIdx : idxs) {
      ifstream fin(Strings::Format("%s/files/%d.log", log1Dir_.c_str(), fileIdx));
      string line;
      Log1 log1Item;
      while (getline(fin, line)) {
        log1Item.parse(line);
        if (log1Item.isTx()) { continue; }
        assert(log1Item.isBlock());
        chain_.push(log1Item.blockHeight_, log1Item.block_.GetHash(), log1Item.block_.hashPrevBlock);
      }
    }
  }

}

