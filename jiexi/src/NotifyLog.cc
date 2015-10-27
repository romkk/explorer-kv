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

#include "NotifyLog.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/file.h>

#include <boost/filesystem.hpp>

namespace fs = boost::filesystem;


/////////////////////////////////  NotifyItem  /////////////////////////////////
NotifyItem::NotifyItem() {
  reset();
}

NotifyItem::NotifyItem(int32_t type, bool isCoinbase, int64_t addressId,
                       int64_t txId, const string &address, const uint256 txhash,
                       int64_t balanceDiff) {
  type_        = type;
  isCoinbase_  = isCoinbase;
  addressId_   = addressId;
  txId_        = txId;
  address_     = address;
  txhash_      = txhash;
  balanceDiff_ = balanceDiff;
}

void NotifyItem::reset() {
  type_        = 0;
  isCoinbase_  = 0;
  addressId_   = 0;
  txId_        = 0;
  address_     = "";
  txhash_      = uint256();
  balanceDiff_ = 0;
}

string NotifyItem::toStrLineWithTime() const {
  return Strings::Format("%s,%d,%d,%lld,%s,%lld,%s,%lld",
                         date("%F %T").c_str(),
                         type_, isCoinbase_ ? 1 : 0,
                         addressId_, address_.c_str(),
                         txId_, txhash_.ToString().c_str(),
                         balanceDiff_);
}

void NotifyItem::parse(const string &line) {
  reset();

  // 按照 ',' 切分，最多切8份
  const vector<string> arr1 = split(line, ',', 8);
  assert(arr1.size() == 8);

  type_        = atoi(arr1[1].c_str());
  isCoinbase_  = atoi(arr1[2].c_str()) != 0 ? 1 : 0;
  addressId_   = atoi64(arr1[3].c_str());
  address_     = arr1[4];
  txId_        = atoi64(arr1[5].c_str());
  txhash_      = uint256(arr1[6]);
  balanceDiff_ = atoi64(arr1[7].c_str());
}


/////////////////////////////////  NotifyProducer  /////////////////////////////////

NotifyProducer::NotifyProducer(const string &dir):
  lockFd_(-1), fileIndex_(-1), fileOffset_(-1L),
  fileHandler_(nullptr), kFileMaxSize_(5 * 1024)
{
  // dir_
  dir_ = dir;

  // 通知文件
  inotifyFile_ = dir_ + "/NOTIFY_NOTIFICATION_LOG";
  {
    FILE *f = fopen(inotifyFile_.c_str(), "w");
    if (f == nullptr) {
      THROW_EXCEPTION_DBEX("create file fail: %s", inotifyFile_.c_str());
    }
    fclose(f);
  }
}

NotifyProducer::~NotifyProducer() {
  if (lockFd_ != -1) {
    flock(lockFd_, LOCK_UN);
    close(lockFd_);
    lockFd_ = -1;
  }

  if (fileHandler_ != nullptr) {
    fsync(fileno(fileHandler_));
    fclose(fileHandler_);
    fileHandler_ = nullptr;
  }
}

void NotifyProducer::init() {
  // 加锁LOCK
  {
    const string lockFile = Strings::Format("%s/LOCK", dir_.c_str());
    lockFd_ = open(lockFile.c_str(), O_RDWR|O_CREAT|O_TRUNC, 0644);
    if (flock(lockFd_, LOCK_EX) != 0) {
      LOG_FATAL("can't lock file: %s", lockFile.c_str());
    }
  }

  //
  // 遍历所有数据文件，找出最近的文件，和最近的记录
  //
  std::set<int32_t> filesIdxs;  // 所有通知数据文件, 文件名：<int>.log
  const string filesDir = Strings::Format("%s/files", dir_.c_str());
  fs::path filesPath(filesDir);
  tryCreateDirectory(filesPath);
  {
    int log1FileIndex = -1;
    for (fs::directory_iterator end, it(filesPath); it != end; ++it) {
      const int idx = atoi(it->path().stem().c_str());
      filesIdxs.insert(idx);

      if (idx > fileIndex_) {
        fileIndex_ = idx;
      }
    }
    fileIndex_ = log1FileIndex;
  }
  LOG_INFO("begin file index: %d", fileIndex_);
}


void NotifyProducer::write(const string &lines) {
  //
  // 写日志
  //
  if (fileIndex_ == -1) {
    fileIndex_ = 0;  // reset to zero as begin index
  }
  if (fileHandler_ == nullptr) {
    const string file = Strings::Format("%s/files/%d.log", dir_.c_str(), fileIndex_);
    fileHandler_ = fopen(file.c_str(), "a");  // append mode
    if (fileHandler_ == nullptr) {
      THROW_EXCEPTION_DBEX("open file failure: %s", file.c_str());
    }
    fseek(fileHandler_, 0L, SEEK_END);
    fileOffset_ = ftell(fileHandler_);
  }

  size_t res = fwrite(lines.c_str(), 1U, lines.length(), fileHandler_);
  if (res != lines.length()) {
    THROW_EXCEPTION_DBEX("[NotifyProducer::write] fwrite return size_t(%llu) is "
                         "NOT match line length: %llu", res, lines.length());
  }
  fileOffset_ += lines.length();
  fflush(fileHandler_);  // fwrite 后执行 fflush 保证其他程序立即可以读取到

  // 写完成后，再执行通知
  // 如果直接监听日志文件的 IN_MODIFY 时间，可能读取不到完整的一行，未写完
  // 就触发了 IN_MODIFY 时间，所以用单独文件去触发通知
  {
    //
    // 只读打开后就关闭掉，会产生一个通知事件，由其他程序捕获
    //     IN_CLOSE_NOWRITE: 一个以只读方式打开的文件或目录被关闭。
    //
    FILE *f = fopen(inotifyFile_.c_str(), "r");
    assert(f != nullptr);
    fclose(f);
  }

  //
  // 切换日志：超过最大文件长度则关闭文件，下次写入时会自动打开新的文件
  //
  if (fileOffset_ > kFileMaxSize_) {
    fsync(fileno(fileHandler_));
    fclose(fileHandler_);
    fileHandler_ = nullptr;
    fileIndex_++;
    LOG_INFO("log1's size(%lld) reach max(%lld), switch to new file index: %d",
             fileOffset_, kFileMaxSize_, fileIndex_);
  }
}

