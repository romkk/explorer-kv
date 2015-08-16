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

#include "Common.h"
#include "bitcoin/core.h"


class Log1 {
public:
  enum {
    TYPE_BLOCK = 1, TYPE_TX = 2
  };

  int32_t type_;
  string  content_;

  CTransaction tx_;

  CBlock  block_;
  int32_t blockHeight_;

  Log1();
  ~Log1();

  bool parse(const string &line);
  bool isTx();
  bool isBlock();
};


class Chain {
  int32_t limit_;
  map<int32_t, uint256> blocks_;

  int32_t getCurHeight() const;
  uint256 getCurHash() const;

public:
  Chain(const int32_t limit);
  ~Chain();

  void push(const int32_t height, const uint256 &hash, const uint256 &prevHash);
};


class Log1Producer {
  /****************** log1 ******************/
  string log1Dir_;
  int log1LockFd_;

  // 最近块高度&哈希值
  int32_t log1BlkHeight_;
  uint256 log1BlkHash_;

  // 最后生成的文件以及游标
  int32_t log1FileIndex_;
  int64_t log1FileOffset_;

  // 最近N个块链
  Chain chain_;


  /****************** log0 ******************/
  string log0Dir_;
  // 最近块高度&哈希值
  int32_t log0BlkHeight_;
  uint256 log0BlkHash_;

  // 最后消费的文件以及游标
  int32_t log0Index_;
  int64_t log0Offset_;

  void writeLog1(const string &line);
  void tryReadLog0();

  void tryRemoveOldLog0();  // 移除旧的log0日志


public:
  Log1Producer();
  ~Log1Producer();

  void init();

  void writeLog1Tx   (const CTransaction &tx);
  void writeLog1Block(const CBlock       &blk);

  // 初始化 log1
  void initLog1();

  // log1 同步上目前的进度，总是先同步上bitcoind，再同步log0
  void syncBitcoind();  // 同步 bitcoind(RPC)
  void syncLog0();      // 同步 log0

};

#endif
