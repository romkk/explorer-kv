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

#include "gtest/gtest.h"
#include "RecognizeBlock.h"

//
// 1. 下载 pools.json
// 2. 关闭 tparser，运行本测试用例
//

TEST(RecognizeBlock, test01) {
  KVDB *kvdb = nullptr;

  // https://raw.githubusercontent.com/blockchain/Blockchain-Known-Pools/master/pools.json
  RecognizeBlock *rb = new RecognizeBlock("./pools.json", kvdb);
  ASSERT_EQ(rb->loadConfigJson(), true);

  delete rb;
}

TEST(RecognizeBlock, test02) {

  KVDB *kvdb = new KVDB(Config::GConfig.get("rocksdb.path"));
  kvdb->open();

  // https://raw.githubusercontent.com/blockchain/Blockchain-Known-Pools/master/pools.json
  RecognizeBlock *rb = new RecognizeBlock("./pools.json", kvdb);
  ASSERT_EQ(rb->loadConfigJson(), true);

  const fbe::Tx *fbtx;
  string key;
  string value;
  MinerInfo minerInfo;

  // main network: 571731cd3c721f77f0d92ee567d7348aa0304be1be4dba23caac958f01d37bb9
  key = Strings::Format("%s%s", KVDB_PREFIX_TX_OBJECT, "571731cd3c721f77f0d92ee567d7348aa0304be1be4dba23caac958f01d37bb9");
  kvdb->get(key, value);
  fbtx = flatbuffers::GetRoot<fbe::Tx>(value.data());
  rb->recognizeCoinbaseTx(fbtx, &minerInfo);
  ASSERT_EQ(minerInfo.poolName_, "Slush");
  ASSERT_EQ(minerInfo.poolLink_, "http://mining.bitcoin.cz/");

  // main network: aff5d70509e845fdd9933ae05d5db85d8a59a8fa22044b05b2cd66392f312cf8
  key = Strings::Format("%s%s", KVDB_PREFIX_TX_OBJECT, "aff5d70509e845fdd9933ae05d5db85d8a59a8fa22044b05b2cd66392f312cf8");
  kvdb->get(key, value);
  fbtx = flatbuffers::GetRoot<fbe::Tx>(value.data());
  rb->recognizeCoinbaseTx(fbtx, &minerInfo);
  ASSERT_EQ(minerInfo.poolName_, "KnCMiner");
  ASSERT_EQ(minerInfo.poolLink_, "https://portal.kncminer.com/pool");

  // main network: df4d6c084d6cd2c2a12c72385b856af63dd7b84fd036b3b554c3eaaa0e940fa7
  key = Strings::Format("%s%s", KVDB_PREFIX_TX_OBJECT, "df4d6c084d6cd2c2a12c72385b856af63dd7b84fd036b3b554c3eaaa0e940fa7");
  kvdb->get(key, value);
  fbtx = flatbuffers::GetRoot<fbe::Tx>(value.data());
  rb->recognizeCoinbaseTx(fbtx, &minerInfo);
  ASSERT_EQ(minerInfo.poolName_, "unknown");
  ASSERT_EQ(minerInfo.poolLink_, "");

  // main network: bbd607bec3826a660fb769a3e285dfc24c775c19bc5baf7aabe34c90f8dd2a2a
  key = Strings::Format("%s%s", KVDB_PREFIX_TX_OBJECT, "bbd607bec3826a660fb769a3e285dfc24c775c19bc5baf7aabe34c90f8dd2a2a");
  kvdb->get(key, value);
  fbtx = flatbuffers::GetRoot<fbe::Tx>(value.data());
  rb->recognizeCoinbaseTx(fbtx, &minerInfo);
  ASSERT_EQ(minerInfo.poolName_, "unknown");
  ASSERT_EQ(minerInfo.poolLink_, "");

  // main network: 2805e462b3cdc8969f4580d8288066f756c46603ecbce72c02357e8835970c14
  key = Strings::Format("%s%s", KVDB_PREFIX_TX_OBJECT, "2805e462b3cdc8969f4580d8288066f756c46603ecbce72c02357e8835970c14");
  kvdb->get(key, value);
  fbtx = flatbuffers::GetRoot<fbe::Tx>(value.data());
  rb->recognizeCoinbaseTx(fbtx, &minerInfo);
  ASSERT_EQ(minerInfo.poolName_, "AntPool");
  ASSERT_EQ(minerInfo.poolLink_, "https://www.antpool.com/");

  delete rb;
  delete kvdb;
}


