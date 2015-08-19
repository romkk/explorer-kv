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

#ifndef Explorer_Log2Producer_h
#define Explorer_Log2Producer_h

#include "Log1Producer.h"
#include "Common.h"
#include "MySQLConnection.h"
#include "Util.h"

#include "bitcoin/core.h"

#include <iostream>
#include <fstream>


////////////////////////////////  TxOutputKey  /////////////////////////////////
class TxOutputKey {
  uint256 hash_;
  int32_t position_;

public:
  TxOutputKey(const uint256 &hash, const int32_t position):
    hash_(hash), position_(position) {
  }

  ~TxOutputKey() {}

  bool operator < (const TxOutputKey &right) const {
    if (hash_ < right.hash_ ||
        (hash_ == right.hash_ && position_ < right.position_)) {
      return true;
    }
    return false;
  }
};


///////////////////////////////  Log2Producer  /////////////////////////////////
class MemTxRepository {
  // 内存中交易，map存储，key为tx哈希，value是tx对象
  map<uint256, CTransaction> memTxs_;

  // 内存所有交易花掉的交易输出
  map<TxOutputKey, uint256> spentOutputs_;

public:
  MemTxRepository();
  ~MemTxRepository();

  // 添加一个交易，如果失败了，会将所有冲突的交易链返回
  bool addTx(const CTransaction &tx, vector<uint256> &conflictTxs);

  // 从内存交易库中删除一个或多个交易
  void removeTxs(const vector<uint256> &txhashs);
};


///////////////////////////////  Log2Producer  /////////////////////////////////
class Log2Producer {
  MySQLConnection db_;
  MemTxRepository memRepo_;

  /* log1 */
  string log1Dir_;

  // 最后消费的文件以及游标
  int32_t log1FileIndex_;
  int64_t log1FileOffset_;

  /* log2 */
  int32_t log2BlockHeight_;
  uint256 log2BlockHash_;

  // 初始化 log2
  void removeUnreadyLog2();
  void initLog2();

  // 同步 log1
  void syncLog1();

  void tryRemoveOldLog1();  // 移除旧的 log1 日志
  void tryReadLog1(vector<string> &lines);

public:
  Log2Producer();
  ~Log2Producer();

  void init();
  void stop();
  void run();
};

#endif
