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
  //
  // 警告：
  // 生成线程仅允许1个，否则与导入模块的tx id会不一致
  //
  const int32_t kThreads = 1;

  // 1. 启动生成线程
  runningProduceThreads_ = kThreads;
  for (int i = 0; i < kThreads; i++) {
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
      for (auto it = txBuf_.begin(); it != txBuf_.end(); it++) {
        buf.push_back(*it);
      }
      txBuf_.clear();
    }

    if (buf.size() == 0) {
      if (runningProduceThreads_ == 0) { break; }
      sleepMs(100);
      continue;
    }

    for (auto &tx : buf) {
      const string txhash = tx.ToString();
      string line;

      //
      // 理论上不会出现重复txhash。（但正式网确实存在两对tx的hash一样）
      // 即使出现一样的，也无关紧要，因为 pre_parser TxHandler采用upper_bound()方法查找ID
      //

      const int32_t tableIdx = HexToDecLast2Bytes(txhash) % 64;
      txIds_[tableIdx]++;
      line = Strings::Format("%s,%lld", txhash.c_str(), txIds_[tableIdx]);

      fprintf(f_, "%s\n", line.c_str());
      cnt++;

      if (cnt % 10000 == 0) {
        LOG_INFO("total tx: %lld", cnt);
      }
    }
    buf.clear();
  }

  LOG_INFO("total tx: %lld", cnt);
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
      if (s > 10000 * 500) {  // 消费线程大约每秒处理10万个地址，防止过速
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

    // 遍历所有交易，提取涉及到的所有地址
    vector<uint256> buf;
    buf.reserve(blk.vtx.size());
    for (auto &tx : blk.vtx) {
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
      buf.push_back(hash);
    }

    string logStr;
    {
      ScopeLock sl(lock_);
      // 遍历所有交易，提取涉及到的所有地址
      for (auto &txhash : buf) {
        txBuf_.push_back(txhash);
      }
      logStr = Strings::Format("height: %6d, size: %7.3f KB, txs: %4lld",
                               curHeight, (double)blkRawHex.length()/(2*1000),
                               blk.vtx.size());
    }
    LOG_INFO("%s", logStr.c_str());
  } /* /while */
  
  runningProduceThreads_--;
}
