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

#ifndef Explorer_NoitfyEventsProducer_h
#define Explorer_NoitfyEventsProducer_h

#include "Log2Producer.h"
#include "Common.h"
#include "MySQLConnection.h"
#include "Util.h"

#include "inotify-cxx.h"

#include "bitcoin/core.h"

#include "rocksdb/cache.h"
#include "rocksdb/compaction_filter.h"
#include "rocksdb/db.h"
#include "rocksdb/options.h"
#include "rocksdb/slice.h"
#include "rocksdb/table.h"
#include "rocksdb/memtablerep.h"

#include <iostream>
#include <fstream>


/////////////////////////////  NotifyEventsProducer  ///////////////////////////
class NotifyEventsProducer {
  string kTableEventsFields_;
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

  /* log2 */
  int32_t currBlockHeight_;
  uint256 currBlockHash_;

  // notify
  string notifyFileLog1Producer_;
  Inotify inotify_;
  thread watchNotifyThread_;

  // kvdb
  rocksdb::DB *kvdb_;
  rocksdb::Options options_;
  rocksdb::WriteOptions writeOptions_;

  // 初始化
  void checkEnvironment();
  void initNotifyEvents();
  void loadMemrepoTxs();

  void tryRemoveOldLog1();  // 移除旧的 log1 日志
  void tryReadLog1(vector<string> &lines, vector<int64_t> &offset);

  void handleTxAccept(Log1 &log1Item);
  void handleTxReject(Log1 &log1Item);

  void handleBlock(Log1 &log1Item);
  void handleBlockAccept  (Log1 &log1Item);

  void handleBlockRollback(const int32_t height, const CBlock &blk);

  void setRawTx(const CTransaction &tx);
  void setRawBlock(const CBlock &blk);
  void getBlockByHash(const uint256 &hash, CBlock &blk);
  void getTxByHash(const uint256 &txHash, CTransaction &tx);

  void threadWatchNotifyFile();

  void clearMempoolTxs();
  void updateCosumeStatus();

  void commitBatch(const size_t expectAffectedRows);

public:
  NotifyEventsProducer();
  ~NotifyEventsProducer();

  void init();
  void stop();
  void run();
};

#endif