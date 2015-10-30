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
#ifndef Explorer_PreAddress_h
#define Explorer_PreAddress_h

#include "Common.h"
#include "bitcoin/core.h"
#include "bitcoin/key.h"

struct PreAddressItem {
  char addrStr_[36];

  PreAddressItem() {
    memset((char *)&addrStr_, 0, sizeof(struct PreAddressItem));
  }
  bool operator<(const PreAddressItem &val) const {
    int r = memcmp(addrStr_, val.addrStr_, 35);
    if (r < 0) {
      return true;
    }
    return false;
  }
};


//
// 用于解决单纯set存放地址去重带来的内存占用过大的问题，1亿的地址大约set占用60G内存
//
class PreAddressHolder {
  size_t count_;
  vector<struct PreAddressItem> addrItems_;
  set<string> *latestAddresses_;  // 地址存在热点效应，所以最近的放入到set中

public:
  PreAddressHolder(): count_(0) {
    latestAddresses_ = new set<string>();
  }
  ~PreAddressHolder() {}

  void adjust();
  size_t count() const { return count_; }

  bool isExist(const string &addressStr);
  void insert(const string &addressStr);
};


// 预处理地址
class PreAddress {
private:
  atomic<bool> running_;
  int32_t height_;
  mutex lockHeight_;

  atomic<int32_t> runningProduceThreads_;
  atomic<int32_t> runningConsumeThreads_;

  vector<string> addrBuf_;
  PreAddressHolder preAddressHolder_;
  vector<int64_t> addressesIds_;
  FILE *f_;
  mutex lock_;

  void threadProcessBlock(const int32_t idx);
  void threadConsumeAddr();

public:
  PreAddress();
  ~PreAddress();

  void run();
  void stop();
};

#endif
