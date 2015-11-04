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

#include "inotify-cxx.h"

#include "bitcoin/core.h"

#include <iostream>
#include <fstream>

//
// log2中交易的类型
// 交易是严格的在这四个状态变迁，允许的变迁过程：
//   accept -> reject
//   accept -> confirm -> unconfirm -> reject
//

// 接收一个未确认交易
#define LOG2TYPE_TX_ACCEPT     100

// 将交易进行确认, 不包含 accept
#define LOG2TYPE_TX_CONFIRM    200

// 将交易解除确认。块回退时，重新变为未确认
#define LOG2TYPE_TX_UNCONFIRM  300

// 拒绝交易。如该交易冲突等。reject的交易必须都是处于未确认态
#define LOG2TYPE_TX_REJECT     400


////////////////////////////////  TxOutputKey  /////////////////////////////////
class TxOutputKey {
  uint256 hash_;
  int32_t position_;

public:
  TxOutputKey(const uint256 &hash, const int32_t position):
    hash_(hash), position_(position) {
  }
  TxOutputKey(const TxOutputKey &right) {
    hash_     = right.hash_;
    position_ = right.position_;
  }

  ~TxOutputKey() {}

  bool operator < (const TxOutputKey &right) const {
    if (hash_ < right.hash_ ||
        (hash_ == right.hash_ && position_ < right.position_)) {
      return true;
    }
    return false;
  }

  string toString() const {
    return Strings::Format("TxOutputKey(hash: %s, %d)",
                           hash_.ToString().c_str(), position_);
  }
};


///////////////////////////////  Log2Producer  /////////////////////////////////
class MemTxRepository {
  // 内存中交易，map存储，key为tx哈希，value是tx对象
  map<uint256, CTransaction> txs_;

  // 内存所有交易花掉的交易输出
  map<TxOutputKey, uint256> spentOutputs_;

  // 待同步至DB的变更记录
  set<uint256> unSyncTxsDelete_;
  set<uint256> unSyncTxsInsert_;

  // 获取已经花费的交易链的末端交易（未被花费的交易）
  uint256 getSpentEndTx(const CTransaction &tx);

public:
  MemTxRepository();
  ~MemTxRepository();

  // 添加一个交易，如果失败了，会将所有冲突的交易链返回
  bool addTx(const CTransaction &tx);

  // 获取冲突交易Hash。由于可能是冲突交易链，本函数返回最深的一个交易。
  uint256 getConflictTx(const CTransaction &tx);

  // 从内存交易库中删除一个或多个交易
  void removeTx (const uint256 &hash);
  void removeTxs(const vector<uint256> &txhashs);

  // 同步至DB
  void syncToDB(MySQLConnection &db);
  // 忽略未同步数据
  void ignoreUnsyncData();

  size_t size() const;
  bool isExist(const uint256 &txhash) const;
};


///////////////////////////////  BlockTimestamp  /////////////////////////////////
class BlockTimestamp {
  int32_t limit_;
  int64_t currMax_;
  map<int32_t, int64_t> blkTimestamps_;  // height <-> timestamp

public:
  BlockTimestamp(const int32_t limit);
  int64_t getMaxTimestamp() const;
  void pushBlock(const int32_t height, const int64_t ts);
  void popBlock();
};


///////////////////////////////  Log2Producer  /////////////////////////////////
class Log2Producer {
  string kTableTxlogs2Fields_;
  mutex lock_;
  Condition changed_;

  atomic<bool> running_;
  MySQLConnection db_;
  MemTxRepository memRepo_;

  /* log1 */
  string log1Dir_;

  // 最后消费的文件以及游标
  int32_t log1FileIndex_;
  int64_t log1FileOffset_;
  int64_t currLog1FileOffset_;

  // 块最大时间戳
  BlockTimestamp blkTs_;

  /* log2 */
  int32_t currBlockHeight_;
  uint256 currBlockHash_;

  // notify
  string notifyFileLog1Producer_;
  string notifyFileTParser_;
  Inotify inotify_;
  thread watchNotifyThread_;

  // 检测环境
  void checkEnvironment();

  // 初始化 log2
  void initLog2();

  // 同步 log1
  void syncLog1();

  void tryRemoveOldLog1();  // 移除旧的 log1 日志
  void tryReadLog1(vector<string> &lines, vector<int64_t> &offset);

  void handleTx(Log1 &log1Item);

  void handleBlock(Log1 &log1Item);
  void handleBlockAccept  (Log1 &log1Item);

  void handleBlockRollback(const int32_t height, const CBlock &blk);
  void _getBlockByHash(const uint256 &hash, CBlock &blk);

  void doNotifyTParser();
  void threadWatchNotifyFile();

  void clearMempoolTxs();
  void updateLog1FileStatus();

  void commitBatch(const size_t expectAffectedRows);

public:
  Log2Producer();
  ~Log2Producer();

  void init();
  void stop();
  void run();
};

#endif
