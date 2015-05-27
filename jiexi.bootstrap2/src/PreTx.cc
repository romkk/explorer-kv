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
#include "PreTx.h"

#include <string>
#include <iostream>
#include <fstream>

#include <boost/thread.hpp>

#include "Parser.h"
#include "Common.h"
#include "Util.h"

#include "bitcoin/base58.h"
#include "bitcoin/util.h"

PreTx::PreTx(): f_(nullptr) {
  const string file = Config::GConfig.get("pre.tx.output.file", "");
  ;
  if (file.length() == 0 || (f_ = fopen(file.c_str(), "w")) == nullptr) {
    THROW_EXCEPTION_DBEX("open file failure: %s", file.c_str());
  }

  // address 分表为64张
  txIds_.resize(64);
  for (int i = 0; i < 64; i++) {
    txIds_[i] = (int64_t)i * BILLION;
  }

  running_ = true;
  height_ = 0;
  runningProduceThreads_ = 0;
  runningConsumeThreads_ = 0;

  txBuf_.reserve(50*10000);
}

PreTx::~PreTx() {
  fclose(f_);
  stop();
  LOG_INFO("PreTx stopped");
}

void PreTx::stop() {
  if (running_) {
    running_ = false;
    LOG_INFO("stop PreTx...");
  }
}

void PreTx::run() {
  const int32_t threads = (int32_t)Config::GConfig.getInt("pre.tx.threads", 2);

  // 1. 启动生成线程
  runningProduceThreads_ = threads;
  for (int i = 0; i < threads; i++) {
    boost::thread t(boost::bind(&PreTx::threadProcessBlock, this, i));
  }
  // 2. 启动消费线程
  runningConsumeThreads_ = 1;
  boost::thread t(boost::bind(&PreTx::threadConsumeAddr, this));

  // 等待生成线程处理完成
  while (runningProduceThreads_ > 0 || runningConsumeThreads_ > 0) {
    sleep(1);
  }
}

void PreTx::threadConsumeAddr() {
  LogScope ls("consume thread");
  vector<uint256> buf;
  buf.reserve(1024*32);
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

    for (auto &tx : buf) {
      const string txhash = tx.ToString();
      string line;
      if (txs_.find(tx) != txs_.end()) {
        continue;  // already exist
      }

      const int32_t tableIdx = HexToDecLast2Bytes(txhash) % 64;
      txs_.insert(tx);
      txIds_[tableIdx]++;
      line = Strings::Format("%s,%lld", txhash.c_str(), txIds_[tableIdx]);

      fprintf(f_, "%s\n", line.c_str());
      cnt++;

      if (cnt % 10000 == 0) {
        LOG_INFO("total address: %lld", cnt);
      }
    }
    buf.clear();
  }

  LOG_INFO("total address: %lld", txs_.size());
  runningConsumeThreads_--;
}

void PreTx::threadProcessBlock(const int32_t idx) {
  const int32_t maxHeight = (int32_t)Config::GConfig.getInt("raw.max.block.height", -1);
  string lsStr = Strings::Format("produce thread %03d...", idx);
  LogScope ls(lsStr.c_str());

  while (running_) {
    int32_t curHeight = 0;
    string blkRawHex;
    int32_t chainId;
    int64_t blockId;

    while (1) {
      size_t s;
      {
        ScopeLock sl(lock_);
        s = txBuf_.size();
      }
      if (s > 10000 * 50) {  // 消费线程大约每秒处理10万个地址，防止过速
        sleepMs(250);
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
      getRawBlockFromDisk(curHeight, &blkRawHex, &chainId, &blockId);
    }

    // 解码Raw Hex
    vector<unsigned char> blockData(ParseHex(blkRawHex));
    CDataStream ssBlock(blockData, SER_NETWORK, BITCOIN_PROTOCOL_VERSION);
    CBlock blk;

    try {
      ssBlock >> blk;
    }
    catch (std::exception &e) {
      THROW_EXCEPTION_DBEX("Block decode failed, height: %d, blockId: %lld",
                           curHeight, blockId);
    }

    string logStr;
    {
      ScopeLock sl(lock_);
      // 遍历所有交易，提取涉及到的所有地址
      for (auto &tx : blk.vtx) {
        txBuf_.push_back(tx.GetHash());
      }
      logStr = Strings::Format("height: %6d, size: %7.3f KB, txs: %4lld",
                               curHeight, (double)blkRawHex.length()/(2*1000),
                               blk.vtx.size());
    }
    LOG_INFO("%s", logStr.c_str());
  } /* /while */
  
  runningProduceThreads_--;
}
