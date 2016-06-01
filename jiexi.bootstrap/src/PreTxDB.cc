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
#include "PreTxDB.h"

#include <string>
#include <iostream>
#include <fstream>

#include <boost/thread.hpp>

#include "Parser.h"
#include "Common.h"
#include "Util.h"

#include "bitcoin/base58.h"
#include "bitcoin/util.h"

PreTxDB::PreTxDB(): db_(nullptr) {
  options_.compression = rocksdb::kSnappyCompression;
  options_.create_if_missing        = true;   // create the DB if it's not already present
  options_.disableDataSync          = true;   // disable syncing of data files

  //
  // open Rocks DB
  //
  // Optimize RocksDB. This is the easiest way to get RocksDB to perform well
  options_.IncreaseParallelism();
  options_.OptimizeLevelStyleCompaction();

  //
  // https://github.com/facebook/rocksdb/blob/master/tools/benchmark.sh
  //
  options_.target_file_size_base    = (size_t)128 * 1024 * 1024;
  options_.max_bytes_for_level_base = (size_t)1024 * 1024 * 1024;
  options_.num_levels = 6;

  options_.write_buffer_size = (size_t)64 * 1024 * 1024;
  options_.max_write_buffer_number  = 8;
  options_.disable_auto_compactions = true;

  // params_bulkload
  options_.max_background_compactions = 16;
  options_.max_background_flushes = 7;
  options_.level0_file_num_compaction_trigger = (size_t)10 * 1024 * 1024;
  options_.level0_slowdown_writes_trigger     = (size_t)10 * 1024 * 1024;
  options_.level0_stop_writes_trigger         = (size_t)10 * 1024 * 1024;

  // initialize BlockBasedTableOptions
  auto cache1 = rocksdb::NewLRUCache(1 * 1024 * 1024 * 1024LL);
  rocksdb::BlockBasedTableOptions bbt_opts;
  bbt_opts.block_cache = cache1;
  bbt_opts.cache_index_and_filter_blocks = 0;
  bbt_opts.block_size = 32 * 1024;
  bbt_opts.format_version = 2;
  options_.table_factory.reset(rocksdb::NewBlockBasedTableFactory(bbt_opts));

  // Optimize write options
  writeOptions_.disableWAL = true;   // disable Write Ahead Log
  writeOptions_.sync       = false;  // use Asynchronous Writes

  // open kvdb
  string kvDir  = Config::GConfig.get("rocksdb.txdb.dir", ".");
  if (kvDir.length() && *std::end(kvDir) == '/') {
    kvDir.resize(kvDir.length() - 1);
  }
  const string kvpath = Strings::Format("%s/rocksdb_alltxs_%d", kvDir.length() ? kvDir.c_str() : ".",
                                        (int32_t)Config::GConfig.getInt("raw.max.block.height", -1));
  LOG_DEBUG("rocksdb path: %s", kvpath.c_str());
  rocksdb::Status s = rocksdb::DB::Open(options_, kvpath, &db_);
  if (!s.ok()) {
    THROW_EXCEPTION_DBEX("open rocks db fail");
  }

  running_ = true;
  height_ = 0;
  runningProduceThreads_ = 0;
  runningConsumeThreads_ = 0;

  txBuf_.reserve(50*10000);
}

PreTxDB::~PreTxDB() {
  delete db_;  // close rocksdb
  stop();
  LOG_INFO("PreTxDB stopped");
}

void PreTxDB::stop() {
  if (running_) {
    running_ = false;
    LOG_INFO("stop PreTxDB...");
  }
}

void PreTxDB::run() {
  // 1. 启动生成线程，固定为4，太多意义不大
  runningProduceThreads_ = 4;
  for (int i = 0; i < runningProduceThreads_; i++) {
    boost::thread t(boost::bind(&PreTxDB::threadProcess, this, i));
  }

  // 2. 启动消费线程，写都需要通过 rocksdb，无需多线程
  runningConsumeThreads_ = 1;
  boost::thread t(boost::bind(&PreTxDB::threadConsume, this));

  // 等待生成线程处理完成
  while (runningProduceThreads_ > 0 || runningConsumeThreads_ > 0) {
    sleep(1);
  }
}

void PreTxDB::threadConsume() {
  LogScope ls("consume thread");
  vector<string> buf;
  int64_t cnt = 0;

  while (running_) {
    {
      ScopeLock sl(lock_);
      while (txBuf_.size()) {
        buf.push_back(*(txBuf_.rbegin()));
        txBuf_.pop_back();
      }
    }

    if (buf.size() == 0) {
      if (runningProduceThreads_ == 0) { break; }
      sleepMs(100);
      continue;
    }

    for (const auto &txhex : buf) {
      CTransaction tx;
      if (!DecodeHexTx(tx, txhex)) {
        THROW_EXCEPTION_DBEX("decode hex tx fail, hex: %s", txhex.c_str());
      }

      // kv key is txhash
      const string key = tx.GetHash().ToString();
      const rocksdb::Slice skey(key.data(), key.size());
      const rocksdb::Slice svalue((const char *)txhex.data(), txhex.size());
      db_->Put(writeOptions_, skey, svalue);

      cnt++;
      if (cnt % 10000 == 0) {
        LOG_INFO("total txs: %lld", cnt);
      }
    }
    buf.clear();
  }

  LOG_INFO("total txs: %lld", cnt);
  runningConsumeThreads_--;
}

void PreTxDB::threadProcess(const int32_t idx) {
  const int32_t maxHeight = (int32_t)Config::GConfig.getInt("raw.max.block.height", -1);
  string lsStr = Strings::Format("produce thread %03d...", idx);
  LogScope ls(lsStr.c_str());

  while (running_) {
    int32_t curHeight = 0;
    string blkRawHex;

    while (1) {
      size_t s;
      {
        ScopeLock sl(lock_);
        s = txBuf_.size();
      }
      if (s > 10000 * 100) {  // 防止过速
        sleepMs(50);
        continue;
      }
      break;
    }

    {
      // 确保多线程间，块数据是严格按照高度递增获取的
      ScopeLock sl(lockHeight_);
      curHeight = height_;

      if (curHeight > maxHeight) {
        LOG_INFO("height(%d) is reach max height in raw file in disk(%d)", curHeight, maxHeight);
        break;
      }

      height_++;
      getRawBlockFromDisk(curHeight, &blkRawHex);
    }

    // 解码Raw Hex
    vector<unsigned char> blockData(ParseHex(blkRawHex));
    CDataStream ssBlock(blockData, SER_NETWORK, BITCOIN_PROTOCOL_VERSION);
    CBlock blk;

    try {
      ssBlock >> blk;
    }
    catch (std::exception &e) {
      THROW_EXCEPTION_DBEX("Block decode failed, height: %d", curHeight);
    }

    // 遍历所有交易，提取涉及到的所有地址
    vector<string> buf;
    for (const auto &tx : blk.vtx) {
      //
      // 硬编码特殊交易处理
      //
      // 1. tx hash: d5d27987d2a3dfc724e359870c6644b40e497bdc0589a033220fe15429d88599
      // 该交易在两个不同的高度块(91812, 91842)中出现过
      // 91842块中有且仅有这一个交易
      //
      // 2. tx hash: e3bf3d07d4b0375638d5f1db5255fe07ba2c4cb067cd81b84ee974b6585fb468
      // 该交易在两个不同的高度块(91722, 91880)中出现过
      //
      const uint256 hash = tx.GetHash();
      if ((curHeight == 91842 && hash == uint256("d5d27987d2a3dfc724e359870c6644b40e497bdc0589a033220fe15429d88599")) ||
          (curHeight == 91880 && hash == uint256("e3bf3d07d4b0375638d5f1db5255fe07ba2c4cb067cd81b84ee974b6585fb468")))
      {
        LOG_WARN("ignore tx, height: %d, hash: %s", curHeight, hash.ToString().c_str());
        continue;
      }
      buf.push_back(EncodeHexTx(tx));
    }

    string logStr;
    {
      ScopeLock sl(lock_);
      // 遍历所有交易，提取涉及到的所有地址
      for (auto &txhex : buf) {
        txBuf_.push_back(txhex);
      }
      logStr = Strings::Format("height: %6d, size: %7.3f KB, txs: %4lld",
                               curHeight, (double)blkRawHex.length()/(2*1000),
                               blk.vtx.size());
    }
    LOG_INFO("%s", logStr.c_str());
  } /* /while */
  
  runningProduceThreads_--;
}
