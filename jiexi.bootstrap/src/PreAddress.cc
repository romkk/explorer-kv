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
#include "PreAddress.h"

#include <string>
#include <iostream>
#include <fstream>

#include <boost/thread.hpp>

#include "Parser.h"
#include "Common.h"
#include "Util.h"

#include "bitcoin/base58.h"
#include "bitcoin/util.h"



////////////////////////////////////////////////////////////////////////////////
//------------------------------ PreAddressHolder ------------------------------
////////////////////////////////////////////////////////////////////////////////

bool PreAddressHolder::isExist(const string &addressStr) {
  // 现在set中查找，找不到则去vector中搜索
  if (latestAddresses_->count(addressStr)) {
    return true;
  }

  if (addrItems_.size() == 0) {
    return false;
  }

  PreAddressItem needle;
  strncpy(needle.addrStr_, addressStr.c_str(), 35);

  //
  // binary_search(), 如果要使用upper_bound()请认真评估和测试（upper_bound不能保证找到等值元素）
  //
  if (std::binary_search(addrItems_.begin(), addrItems_.end(), needle)) {
    return true;
  }

  return false;
}

void PreAddressHolder::insert(const string &addressStr) {
  latestAddresses_->insert(addressStr);
  count_++;

  // 每500万调整一次
  if (latestAddresses_->size() >= 500 * 10000) {
    LOG_INFO("adjust memory, move address from set to vector");
    adjust();
  }
}

void PreAddressHolder::adjust() {
  size_t currSize = addrItems_.size();

  // resize vector
  addrItems_.resize(currSize + latestAddresses_->size());
  LOG_INFO("addrItems_.size(): %llu", addrItems_.size());

  // push new address from set to vector
  for (const auto &it : *latestAddresses_) {
    strncpy(addrItems_[currSize++].addrStr_, it.c_str(), 35);
  }

  //
  // clear set
  // 释放对象 + tcmalloc，两个手段可以保障内存占用不会那么夸张。（原来1亿地址消耗58G左右内存，现在不到15G内存）
  //
  delete latestAddresses_;
  latestAddresses_ = new set<string>();

  // sort vector, for binary search
  std::sort(addrItems_.begin(), addrItems_.end());
}



////////////////////////////////////////////////////////////////////////////////
//-------------------------------- PreAddress ---------------------------------
////////////////////////////////////////////////////////////////////////////////

PreAddress::PreAddress(): f_(nullptr) {
  const string file = Config::GConfig.get("pre.address.output.file", "");
  ;
  if (file.length() == 0 || (f_ = fopen(file.c_str(), "w")) == nullptr) {
    THROW_EXCEPTION_DBEX("open file failure: %s", file.c_str());
  }

  running_ = true;
  height_ = 0;
  runningProduceThreads_ = 0;
  runningConsumeThreads_ = 0;

  addrBuf_.reserve(50*10000);
}

PreAddress::~PreAddress() {
  fclose(f_);
  stop();
  LOG_INFO("PreAddress stopped");
}

void PreAddress::stop() {
  if (running_) {
    running_ = false;
    LOG_INFO("stop PreAddress...");
  }
}

void PreAddress::run() {
  const int32_t threads = (int32_t)Config::GConfig.getInt("pre.address.threads", 2);

  // 1. 启动生成线程
  runningProduceThreads_ = threads;
  for (int i = 0; i < threads; i++) {
    boost::thread t(boost::bind(&PreAddress::threadProcessBlock, this, i));
  }
  // 2. 启动消费线程
  runningConsumeThreads_ = 1;
  boost::thread t(boost::bind(&PreAddress::threadConsumeAddr, this));

  // 等待生成线程处理完成
  while (runningProduceThreads_ > 0 || runningConsumeThreads_ > 0) {
    sleep(1);
  }
}

void PreAddress::threadConsumeAddr() {
  LogScope ls("consume thread");
  int64_t cnt = 0;

  while (running_) {
    vector<string> buf;
    {
      ScopeLock sl(lock_);
      while (addrBuf_.size()) {
        buf.push_back(*(addrBuf_.rbegin()));
        addrBuf_.pop_back();
      }
    }

    if (buf.size() == 0) {
      if (runningProduceThreads_ == 0) { break; }
      sleepMs(100);
      continue;
    }

    for (auto &addr : buf) {
      if (preAddressHolder_.isExist(addr)) {
        continue;
      }
      preAddressHolder_.insert(addr);

      fprintf(f_, "%s\n", addr.c_str());
      cnt++;

      if (cnt % 10000 == 0) {
        LOG_INFO("total address: %lld", cnt);
      }
    }
  }

  LOG_INFO("total address: %llu", preAddressHolder_.count());
  runningConsumeThreads_--;
}

void PreAddress::threadProcessBlock(const int32_t idx) {
  const int32_t maxHeight = (int32_t)Config::GConfig.getInt("raw.max.block.height", -1);
  string lsStr = Strings::Format("produce thread %03d...", idx);
  LogScope ls(lsStr.c_str());

  while (running_) {
    int32_t curHeight = 0;
    string blkRawHex;
    int32_t chainId;
    int64_t blockId;

    while (running_) {
      size_t s;
      {
        ScopeLock sl(lock_);
        s = addrBuf_.size();
      }
      if (s > 10000 * 50) {  // 消费线程大约每秒处理10万个地址，防止过速
        sleepMs(250);
        continue;
      }
      break;
    }

    if (!running_) { break; }

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
    set<string> blkAddress;

    try {
      ssBlock >> blk;
    }
    catch (std::exception &e) {
      THROW_EXCEPTION_DBEX("Block decode failed, height: %d, blockId: %lld",
                           curHeight, blockId);
    }

    // 遍历所有交易，提取涉及到的所有地址
    for (auto &tx : blk.vtx) {
      for (auto &out : tx.vout) {
        txnouttype type;
        vector<CTxDestination> addresses;
        int nRequired;
        if (!ExtractDestinations(out.scriptPubKey, type, addresses, nRequired)) {
          LOG_WARN("extract destinations failure, hash: %s",
                   tx.GetHash().ToString().c_str());
          continue;
        }
        for (auto &addr : addresses) {  // multiSig 可能由多个输出地址
          const string s = CBitcoinAddress(addr).ToString();
          blkAddress.insert(s);
        }
      }
    }

    string logStr;
    {
      ScopeLock sl(lock_);
      for (auto &it : blkAddress) {
        addrBuf_.push_back(it);
      }
      logStr = Strings::Format("height: %6d, size: %7.3f KB, txs: %4lld, addr: %lld",
                               curHeight, (double)blkRawHex.length()/(2*1000),
                               blk.vtx.size(), blkAddress.size());
    }
    LOG_INFO("%s", logStr.c_str());
  } /* /while */
  
  runningProduceThreads_--;
}
