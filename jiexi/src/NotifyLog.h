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

#ifndef Explorer_NotifyLog_h
#define Explorer_NotifyLog_h

#include "Common.h"
#include "Util.h"

#include "inotify-cxx.h"
#include "bitcoin/core.h"

#include <iostream>
#include <fstream>


#define NOTIFY_EVENT_BLOCK_ACCEPT 10
#define NOTIFY_EVENT_BLOCK_REJECT 11

#define NOTIFY_EVENT_TX_ACCEPT    20
#define NOTIFY_EVENT_TX_CONFIRM   21
#define NOTIFY_EVENT_TX_UNCONFIRM 22
#define NOTIFY_EVENT_TX_REJECT    23


/////////////////////////////////  NotifyItem  /////////////////////////////////
// 定义参考： NOTIFY.md
class NotifyItem {
public:
  uint32_t timestamp_;
  int32_t type_;

  int32_t height_;
  int64_t amount_;
  uint256 hash_;     // tx or block
  char address_[36];

public:
  NotifyItem();
  void loadtx(const int32_t type, const string &address,
              const uint256 &hash, const int32_t height,  const int64_t amount);
  void loadblock(const int32_t type, const uint256 &hash, const int32_t height);

  void reset();
  string toStr() const;
  bool parse(const string &line);
};

///////////////////////////////  NotifyLogReader  ///////////////////////////////
class NotifyLogReader {
  string logDir_;
  FILE *fileHandler_;

public:
  NotifyLogReader(string &logDir);
  ~NotifyLogReader();

  void readLines(int32_t currFileIndex, int64_t currFileOffset,
                 vector<string> *lines, vector<int64_t> *fileOffset);
  bool isNewFileExist(int32_t currFileIndex);
  void tryRemoveOldFiles(int32_t currFileIndex);
};

///////////////////////////////////  Notify  ///////////////////////////////////
class Notify {
  atomic<bool> running_;
  mutex lock_;

  std::map<string, std::set<int32_t> > addressTable_;  // address -> [app_id, ...]
  std::set<int32_t> appIds_;

  // 上游生成日志，触发文件通知去消费
  string iNotifyFile_;
  Condition changed_;
  Inotify inotify_;

  // 通知事件日志相关
  string logDir_;
  int32_t logFileIndex_;
  int64_t logFileOffset_;
  NotifyLogReader logReader_;

  MySQLConnection db_;

  thread threadWatchNotify_;
  thread threadConsumeNotifyLogs_;

  void threadWatchNotify();
  void threadConsumeNotifyLogs();

  void loadAddressTableFromDB();

  void handleBlockEvent(NotifyItem &item);
  void handleTxEvent(NotifyItem &item);

  void updateStatus();
  void getStatus();

public:
  Notify();
  ~Notify();

  void init();
  void setup();
  void stop();

  bool insertAddress(const int32_t appID, const char *address, bool sync2mysql=true);
  bool removeAddress(const int32_t appID, const char *address);


};


///////////////////////////////  NotifyProducer  ///////////////////////////////
class NotifyProducer {
  // 通知文件的目录及游标
  string dir_;
  int lockFd_;
  int32_t fileIndex_;
  int64_t fileOffset_;
  FILE *fileHandler_;

  int64_t kFileMaxSize_;

  // inotify 通知文件
  string inotifyFile_;

public:
  NotifyProducer(const string &dir);
  ~NotifyProducer();

  void init();
  void write(const string &lines);
};

#endif
