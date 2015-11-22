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

#include "KVDB.h"

#include "rocksdb/c.h"

//
// TODO: 采用列族(column family)来优化相同的key,
//       例如： 00_{tx_hash}, 01_{tx_hash}, 02_{tx_hash}
//


KVDB::KVDB(const string &dbPath): db_(nullptr), kDBPath_(dbPath) {
  // Optimize RocksDB. This is the easiest way to get RocksDB to perform well
  options.IncreaseParallelism();
  options.OptimizeLevelStyleCompaction();

  // create the DB if it's not already present
  options.create_if_missing = true;
}

void KVDB::open() {
  // open DB
  rocksdb::Status s = rocksdb::DB::Open(options, kDBPath_, &db_);
  if (!s.ok()) {
    THROW_EXCEPTION_DBEX("open rocks db fail");
  }
}

void KVDB::getPrevTxOutputs(const CTransaction &tx,
                            vector<string> &prevTxsData,
                            vector<const fbe::TxOutput *> &prevTxOutputs) {
  std::map<string /*tx hash*/, const fbe::Tx *> prevTxs;

  vector<rocksdb::Slice> keys;
  vector<string> keysStr;

  {
    std::set<uint256> prevTxHashSet;
    for (const auto &input : tx.vin) {
      if (prevTxHashSet.count(input.prevout.hash) != 0) {
        continue;
      }
      prevTxHashSet.insert(input.prevout.hash);

      keysStr.push_back(KVDB_PREFIX_TX_OBJECT + input.prevout.hash.ToString());
      keys.push_back(rocksdb::Slice(std::end(keysStr)->data(), std::end(keysStr)->length()));
    }
  }

  db_->MultiGet(rocksdb::ReadOptions(), keys, &prevTxsData);

  for (size_t i = 0; i < keys.size(); i++) {
    const fbe::Tx *prevTx = flatbuffers::GetRoot<fbe::Tx>(prevTxsData[i].data());
    prevTxs.insert(std::make_pair(keys[i].ToString(), prevTx));
  }

  for (const auto &input : tx.vin) {
    const uint256 &phash = input.prevout.hash;
    const uint32_t &ppos = input.prevout.n;
    const auto output = prevTxs[phash.ToString()]->outputs()->operator[](ppos);
    prevTxOutputs.push_back(output);
  }

  assert(tx.vin.size() == prevTxOutputs.size());
}
