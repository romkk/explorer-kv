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

#ifndef Explorer_NotifyLog_h
#define Explorer_NotifyLog_h

#include "Common.h"
#include "Util.h"

#include "inotify-cxx.h"
#include "bitcoin/core.h"

#include <iostream>
#include <fstream>


/////////////////////////////////  NotifyItem  /////////////////////////////////
class NotifyItem {
  int32_t type_;  // LOG2TYPE_TX_xxxx
  bool isCoinbase_;

  int64_t addressId_;
  int64_t txId_;

  string address_;
  uint256 txhash_;

  int64_t balanceDiff_;

public:
  NotifyItem();
  NotifyItem(int32_t type, bool isCoinbase, int64_t addressId, int64_t txId,
             const string &address, const uint256 txhash, int64_t balanceDiff);

  void reset();
  string toStrLineWithTime() const;
  void parse(const string &line);
};


///////////////////////////////  NotifyProducer  ///////////////////////////////
class NotifyProducer {

};

#endif
