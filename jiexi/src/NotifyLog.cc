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

#include "NotifyLog.h"

/////////////////////////////////  NotifyItem  /////////////////////////////////
NotifyItem::NotifyItem() {
  reset();
}

NotifyItem::NotifyItem(int32_t type, bool isCoinbase, int64_t addressId,
                       int64_t txId, const string &address, const uint256 txhash,
                       int64_t balanceDiff) {
  type_        = type;
  isCoinbase_  = isCoinbase;
  addressId_   = addressId;
  txId_        = txId;
  address_     = address;
  txhash_      = txhash;
  balanceDiff_ = balanceDiff;
}

void NotifyItem::reset() {
  type_        = 0;
  isCoinbase_  = 0;
  addressId_   = 0;
  txId_        = 0;
  address_     = "";
  txhash_      = uint256();
  balanceDiff_ = 0;
}

string NotifyItem::toStrLineWithTime() const {
  return Strings::Format("%s,%d,%d,%lld,%s,%lld,%s,%lld",
                         date("%F %T").c_str(),
                         type_, isCoinbase_ ? 1 : 0,
                         addressId_, address_.c_str(),
                         txId_, txhash_.ToString().c_str(),
                         balanceDiff_);
}

void NotifyItem::parse(const string &line) {
  reset();

  // 按照 ',' 切分，最多切8份
  const vector<string> arr1 = split(line, ',', 8);
  assert(arr1.size() == 8);

  type_        = atoi(arr1[1].c_str());
  isCoinbase_  = atoi(arr1[2].c_str()) != 0 ? 1 : 0;
  addressId_   = atoi64(arr1[3].c_str());
  address_     = arr1[4];
  txId_        = atoi64(arr1[5].c_str());
  txhash_      = uint256(arr1[6]);
  balanceDiff_ = atoi64(arr1[7].c_str());
}

