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
#ifndef Explorer_PreTx_h
#define Explorer_PreTx_h

#include "Common.h"
#include "bitcoin/core.h"
#include "bitcoin/key.h"

// 预处理交易
class PreTx {
private:
  atomic<bool> running_;
  int32_t height_;
  mutex lockHeight_;

  atomic<int32_t> runningProduceThreads_;
  atomic<int32_t> runningConsumeThreads_;

  vector<uint256> txBuf_;
  set<uint256>    txs_;
  vector<int64_t> txIds_;
  FILE *f_;
  mutex lock_;

  void threadProcessBlock(const int32_t idx);
  void threadConsumeAddr();

public:
  PreTx();
  ~PreTx();

  void run();
  void stop();
};

#endif