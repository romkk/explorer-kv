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

#include <string>
#include <fstream>
#include <streambuf>

#include "RecognizeBlock.h"
#include "utilities_js.hpp"

RecognizeBlock::RecognizeBlock(const string &configFilePath, KVDB *kvdb):
configFilePath_(configFilePath), kvdb_(kvdb) {
}

RecognizeBlock::~RecognizeBlock() {}

bool RecognizeBlock::loadConfigJson() {
  std::ifstream filein(configFilePath_);
  std::string jsonStr((std::istreambuf_iterator<char>(filein)), std::istreambuf_iterator<char>());

  JsonNode root;
  if (!JsonNode::parse(jsonStr.c_str(), jsonStr.c_str() + jsonStr.length(), root)) {
    LOG_ERROR("load json config fail: %s", configFilePath_.c_str());
    return false;
  }

  //
  // payout_addresses
  //
  payoutAddresses_.clear();
  for (auto item : *root["payout_addresses"].children()) {
    const string address = string(item.key_start(), item.key_size());
    payoutAddresses_[address] = MinerInfo(item["name"].str(), item["link"].str());
  }

  //
  // coinbase_tags
  //
  coinbaseTags_.resize(root["coinbase_tags"].children()->size());
  size_t i = 0;
  for (auto item : *root["coinbase_tags"].children()) {
    coinbaseTags_[i].keyword_ = string(item.key_start(), item.key_size());
    coinbaseTags_[i].minerInfo_.poolName_ = item["name"].str();
    coinbaseTags_[i].minerInfo_.poolLink_ = item["link"].str();
    i++;
  }

  return true;
}

void RecognizeBlock::recognizeBlocks(const int32_t beginHeight, const int32_t endHeight,
                                     bool stopWhenReachLast) {
  const string keyStart = Strings::Format("%s%010d", KVDB_PREFIX_BLOCK_HEIGHT, beginHeight);
  const string keyEnd   = Strings::Format("%s%010d", KVDB_PREFIX_BLOCK_HEIGHT, endHeight);

  vector<string> keys, values;
  kvdb_->range(keyStart, keyEnd, 99999999, keys, values);
  for (auto hash : values) {
    // check if block has already recognized
    if (stopWhenReachLast) {
      const string key15 = Strings::Format("%s%s", KVDB_PREFIX_BLOCK_RELAYEDBY, hash.c_str());
      string value;
      if (kvdb_->getMayNotExist(key15, value) == true /* exist */) {
        return;  // meet last recognized block
      }
    }

    // get coinbase tx hash
    const string keyTxsStr = Strings::Format("%s%s_0", KVDB_PREFIX_BLOCK_TXS_STR, hash.c_str());
    string blockTxs;
    kvdb_->get(keyTxsStr, blockTxs);
    const string cbTxHash = blockTxs.substr(0, 64);

    // get coinbase tx fb object
    const string keyCbTx = Strings::Format("%s%s", KVDB_PREFIX_TX_OBJECT, cbTxHash.c_str());
    string cbtxvalue;
    kvdb_->get(keyCbTx, cbtxvalue);
    auto fbtx = flatbuffers::GetRoot<fbe::Tx>(cbtxvalue.data());

    recognizeCoinbaseTx(hash, fbtx);
  }
}

void RecognizeBlock::recognizeCoinbaseTx(const fbe::Tx *cbtx, MinerInfo *info) {
//  ScopeLock sl(lock_);
  static MinerInfo defaultInfo("unknown", "");

  // 优先识别输出地址，再识别输入string
  for (auto it : *(cbtx->outputs())) {
    for (auto addr : *(it->addresses())) {
      auto findIt = payoutAddresses_.find(addr->str());
      if (findIt == payoutAddresses_.end()) {
        continue;
      }
      *info = findIt->second;
      return;
    }
  }

  // 识别coinbase String
  for (auto in : *(cbtx->inputs())) {
    const string hex = in->script_hex()->str();
    vector<char> bin;
    Hex2Bin(hex.c_str(), bin);
    const string scriptStr(bin.data(), bin.size());
    for (auto it : coinbaseTags_) {
      if (scriptStr.find(it.keyword_) == string::npos) {
        continue;
      }
      *info = it.minerInfo_;
      return;
    }
  }

  // 即使未识别，依然填写默认值，这样才可以界定识别到哪里了
  *info = defaultInfo;
}

void RecognizeBlock::recognizeCoinbaseTx(const string &blockHash, const fbe::Tx *cbtx) {
  MinerInfo info;
  recognizeCoinbaseTx(cbtx, &info);

  // save to kvdb
  flatbuffers::FlatBufferBuilder fbb;
  auto fb_name = fbb.CreateString(info.poolName_);
  auto fb_link = fbb.CreateString(info.poolLink_);
  fbe::RelayedByBuilder relayedByBuilder(fbb);
  relayedByBuilder.add_name(fb_name);
  relayedByBuilder.add_link(fb_link);
  fbb.Finish(relayedByBuilder.Finish());

  // 15\_{block\_hash}
  // 如果存在，则覆盖该值
  string key = Strings::Format("%s%s", KVDB_PREFIX_BLOCK_RELAYEDBY, blockHash.c_str());
  kvdb_->set(key, fbb.GetBufferPointer(), fbb.GetSize());
  fbb.Clear();

  LOG_INFO("recognize block: %d, %s, %s", cbtx->block_height(), blockHash.c_str(),
           info.poolName_.c_str());
}

