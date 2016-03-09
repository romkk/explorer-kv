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
#include "Util.h"

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

void NotifyItem::loadtx(const int32_t type, const string &address,
                        const uint256 &hash, const int32_t height, const int64_t amount) {
  reset();
  timestamp_ = (uint32_t)time(nullptr);
  type_   = type;
  height_ = height;
  amount_ = amount;
  hash_   = hash;
  snprintf(address_, sizeof(address_), "%s", address.c_str());
}

void NotifyItem::loadblock(const int32_t type, const uint256 &hash, const int32_t height) {
  reset();
  timestamp_ = (uint32_t)time(nullptr);
  type_   = type;
  height_ = height;
  hash_   = hash;
}

void NotifyItem::reset() {
  type_ = -1;
  height_ = -1;
  amount_ = 0;
  hash_ = uint256();
  memset(address_, 0, sizeof(address_));
}

string NotifyItem::toStr() const {
  // tx
  if (type_ >= 20 && type_ <= 23) {
    return Strings::Format("%s,%d,%s,%d,%s,%lld",
                           date("%F %T", timestamp_).c_str(), type_, address_, height_,
                           hash_.ToString().c_str(), amount_);
  }
  // block
  else if (type_ >= 10 && type_ <= 11) {
    return Strings::Format("%s,%d,%d,%s",
                           date("%F %T", timestamp_).c_str(), type_, height_,
                           hash_.ToString().c_str());
  }

  // should not be here
  LOG_ERROR("[NotifyItem::toStr] unknown type");
  return "";
}

void NotifyItem::parse(const string &line) {
  reset();

  // 按照 ',' 切分
  const vector<string> arr = split(line, ',', 6);

  timestamp_ = (uint32_t)str2time(arr[0].c_str(), "%F %T");
  type_ = atoi(arr[1].c_str());

  // tx
  if (type_ >= 20 && type_ <= 23) {
    assert(arr.size() == 6);
    snprintf(address_, sizeof(address_), "%s", arr[2].c_str());
    height_ = atoi(arr[3].c_str());
    hash_ = uint256(arr[4].c_str());
    amount_ = atoll(arr[5].c_str());
    return;
  }

  // block
  if (type_ >= 10 && type_ <= 11) {
    assert(arr.size() == 4);
    height_ = atoi(arr[2].c_str());
    hash_ = uint256(arr[3].c_str());
    return;
  }

  // should not be here
  LOG_ERROR("[NotifyItem::parse] unknown type");
}


/////////////////////////////////  NotifyProducer  /////////////////////////////////

NotifyProducer::NotifyProducer(const string &dir):
  lockFd_(-1), fileIndex_(-1), fileOffset_(-1L),
  fileHandler_(nullptr), kFileMaxSize_(100 * 1024 * 1024)
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
  const string filesDir = Strings::Format("%s/files", dir_.c_str());
  fs::path filesPath(filesDir);
  tryCreateDirectory(filesPath);
  {
    for (fs::directory_iterator end, it(filesPath); it != end; ++it) {
      const int idx = atoi(it->path().stem().c_str());
      if (idx > fileIndex_) {
        fileIndex_ = idx;
      }
    }
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




/////////////////////////////////  NotifyItem  /////////////////////////////////
NotifyLogReader::NotifyLogReader(string &logDir):
//fileIndex_(0), fileOffset_(0),
fileHandler_(nullptr), logDir_(logDir)
{}

NotifyLogReader::~NotifyLogReader() {
  if (fileHandler_ != nullptr) {
    fclose(fileHandler_);
    fileHandler_ = nullptr;
  }
}

void NotifyLogReader::readLines(int32_t currFileIndex, int64_t currFileOffset,
                                vector<string> *lines, vector<int64_t> *fileOffset) {
  const string currFile = Strings::Format("%s/files/%d.log",
                                          logDir_.c_str(), currFileIndex);
  lines->clear();
  fileOffset->clear();

  //
  // 打开文件并尝试读取新行
  //
  ifstream logStream(currFile);
  if (!logStream.is_open()) {
    THROW_EXCEPTION_DBEX("open file failure: %s", currFile.c_str());
  }
  logStream.seekg(currFileOffset);
  string line;

  while (getline(logStream, line)) {  // getline()读不到内容，则会关闭 ifstream
    if (logStream.eof()) {
      // eof 表示没有遇到 \n 就抵达文件尾部了，通常意味着未完全读取一行
      // 读取完最后一行后，再读取一次，才会导致 eof() 为 true
      break;
    }

    // offset肯定是同一个文件内的，不可能跨文件
    lines->push_back(line);
    fileOffset->push_back(logStream.tellg());

    if (lines->size() > 500) {  // 每次最多处理500条日志
      break;
    }
  } /* /while */

  if (lines->size() > 0) {
    LOG_DEBUG("load notify items: %lld", lines->size());
    return;
  }
}

bool NotifyLogReader::isNewFileExist(int32_t currFileIndex) {
  const string nextFile = Strings::Format("%s/files/%d.log",
                                          logDir_.c_str(), currFileIndex + 1);
  return fs::exists(fs::path(nextFile));
}

void NotifyLogReader::tryRemoveOldFiles(int32_t currFileIndex) {
  const int32_t keepLogNum = 100;  // 最多保留文件数量
  int32_t fileIdx = currFileIndex - keepLogNum;

  // 遍历，删除所有小序列号的文件
  while (fileIdx >= 0) {
    const string file = Strings::Format("%s/files/%d.log", logDir_.c_str(), fileIdx--);
    if (!fs::exists(fs::path(file))) {
      break;
    }

    // try delete
    LOG_INFO("remove old log: %s", file.c_str());
    if (!fs::remove(fs::path(file))) {
      THROW_EXCEPTION_DBEX("remove old log failure: %s", file.c_str());
    }
  }
}
















