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

#ifndef Explorer_Log1Producer_h
#define Explorer_Log1Producer_h

#include "BitcoinRpc.h"
#include "Common.h"
#include "Util.h"
#include "utilities_js.hpp"

#include "inotify-cxx.h"

#include "bitcoin/core.h"

#include <iostream>
#include <fstream>


//////////////////////////////////  Log1  //////////////////////////////////////
class Log1 {
  CTransaction tx_;
  CBlock  block_;

  void reset();

public:
  enum {
    TYPE_BLOCK = 1, TYPE_TX = 2
  };

  int32_t type_;
  string  content_;

  int32_t blockHeight_;

  Log1();
  ~Log1();

  void parse(const string &line);
  bool isTx();
  bool isBlock();
  const CBlock &getBlock();
  const CTransaction &getTx();
  string toString();
};


//////////////////////////////////  Chain  /////////////////////////////////////
class Chain {
  int32_t limit_;
  map<int32_t, uint256> blocks_;

public:
  Chain(const int32_t limit);
  ~Chain();

  void pushFirst(const int32_t height, const uint256 &hash);
  void push(const int32_t height, const uint256 &hash, const uint256 &prevHash);
  int32_t getCurHeight() const;
  uint256 getCurHash() const;
  size_t size() const;
  void pop();
};


//////////////////////////////  Log1Producer  //////////////////////////////////
class Log1Producer {
  mutex lock_;
  Condition changed_;
  atomic<bool> running_;

  /****************** log1 ******************/
  string log1Dir_;
  int log1LockFd_;
  FILE *log1FileHandler_;

  // 最后生成的文件索引
  int32_t log1FileIndex_;

  // 最近N个块链
  Chain chain_;

  /****************** log0 ******************/
  string log0Dir_;

  // notify
  string notifyFileLog0_;
  string notifyFileLog2Producer_;
  Inotify inotify_;
  thread watchNotifyThread_;

  // 最后消费的文件以及游标
  int32_t log0FileIndex_;
  int64_t log0FileOffset_;
  time_t  log0BeginFileLastModifyTime_;

  void writeLog1(const int32_t type, const string &line);
  void writeLog1Tx   (const CTransaction &tx);
  void writeLog1Block(const int32_t height, const CBlock &blk);

  void tryRemoveOldLog0();  // 移除旧的log0日志
  void tryReadLog0(vector<string> &lines);

  void doNotifyLog2Producer();
  void threadWatchNotifyFile();

  // 初始化 log1
  void initLog1();

  // log1 同步上目前的进度，总是先同步上bitcoind，再同步log0
  void syncBitcoind();  // 同步 bitcoind(RPC)
  void syncLog0();      // 同步 log0

public:
  Log1Producer();
  ~Log1Producer();

  void init();
  void stop();
  void run();
};

#endif
