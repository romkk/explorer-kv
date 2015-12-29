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
#ifndef EXPLORER_RECOGNIZEBLOCK_H_
#define EXPLORER_RECOGNIZEBLOCK_H_

#include "Common.h"
#include "explorer_generated.h"
#include "KVDB.h"

struct MinerInfo {
  string poolName_;
  string poolLink_;

  MinerInfo() {}

  MinerInfo(const string &poolName, const string &poolLink):
  poolName_(poolName), poolLink_(poolLink) {}

  MinerInfo(const MinerInfo &r) {
    poolName_ = r.poolName_;
    poolLink_ = r.poolLink_;
  }
};

struct MinerCoinbaseTag {
  string keyword_;
  MinerInfo minerInfo_;

//  MinerCoinbaseTag() {}
//
//  MinerCoinbaseTag(const string &keyword, const MinerInfo &minerInfo):
//  keyword_(keyword), minerInfo_(minerInfo) {}
//
//  MinerCoinbaseTag(const MinerCoinbaseTag &r) {
//    keyword_   = r.keyword_;
//    minerInfo_ = r.minerInfo_;
//  }
};

class RecognizeBlock {
//  mutex lock_;
  string configFilePath_;
  KVDB *kvdb_;

  map<string, MinerInfo>   payoutAddresses_;
  vector<MinerCoinbaseTag> coinbaseTags_;

  void recognizeCoinbaseTx(const string &blockHash, const fbe::Tx *cbtx);

public:
  RecognizeBlock(const string &configFilePath, KVDB *kvdb);
  ~RecognizeBlock();

  void recognizeCoinbaseTx(const fbe::Tx *cbtx, MinerInfo *info);
  bool loadConfigJson();
  void recognizeBlocks(const int32_t beginHeight, const int32_t endHeight, bool stopWhenReachLast);
};

#endif
