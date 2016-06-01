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
#ifndef Explorer_PreTxDB_h
#define Explorer_PreTxDB_h

#include "Common.h"
#include "bitcoin/core.h"
#include "bitcoin/key.h"

#include "rocksdb/cache.h"
#include "rocksdb/compaction_filter.h"
#include "rocksdb/db.h"
#include "rocksdb/options.h"
#include "rocksdb/slice.h"
#include "rocksdb/table.h"
#include "rocksdb/memtablerep.h"

//
// 预处理生成所有交易的库: 遍历所有块，把 txhash -> txHex 存入KV数据库
//
class PreTxDB {
private:
  atomic<bool> running_;
  int32_t height_;
  mutex lockHeight_;

  atomic<int32_t> runningProduceThreads_;
  atomic<int32_t> runningConsumeThreads_;

  vector<string> txBuf_;  // tx hex
  mutex lock_;

  // kvdb
  rocksdb::DB *db_;
  rocksdb::Options options_;
  rocksdb::WriteOptions writeOptions_;

  void threadProcess(const int32_t idx);
  void threadConsume();

public:
  PreTxDB();
  ~PreTxDB();

  void run();
  void stop();
};

#endif