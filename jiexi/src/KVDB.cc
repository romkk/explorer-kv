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
  options_.IncreaseParallelism();
  options_.OptimizeLevelStyleCompaction();

  // create the DB if it's not already present
  options_.create_if_missing = true;
}

void KVDB::open() {
  // open DB
  rocksdb::Status s = rocksdb::DB::Open(options_, kDBPath_, &db_);
  if (!s.ok()) {
    THROW_EXCEPTION_DBEX("open rocks db fail");
  }
}

bool KVDB::keyExist(const string &key) {
  string value;
  // If the key definitely does not exist in the database, then this method
  // returns false, else true.
  if (db_->KeyMayExist(rocksdb::ReadOptions(), key, &value)) {
    return true;
  }
  return false;
}

void KVDB::del(const string &key) {
  rocksdb::Status s = db_->Delete(rocksdb::WriteOptions(), key);
  if (!s.ok()) {
    THROW_EXCEPTION_DBEX("delelte key: %s fail", key.c_str());
  }
}

void KVDB::get(const string &key, string &value) {
  rocksdb::Status s = db_->Get(rocksdb::ReadOptions(), key, &value);
  if (s.IsNotFound()) {
    THROW_EXCEPTION_DBEX("not found key: %s", key.c_str());
  }
}

void KVDB::getMayNotExist(const string &key, string &value) {
  db_->Get(rocksdb::ReadOptions(), key, &value);
}

void KVDB::set(const string &key, const string &value) {
  rocksdb::Status s = db_->Put(rocksdb::WriteOptions(), key, value);
  if (!s.ok()) {
    THROW_EXCEPTION_DBEX("set key fail, key: %s", key.c_str());
  }
}

void KVDB::set(const string &key, const uint8_t *data, const size_t length) {
  rocksdb::Slice value((const char *)data, length);
  rocksdb::Status s = db_->Put(rocksdb::WriteOptions(), key, value);
  if (!s.ok()) {
    THROW_EXCEPTION_DBEX("set key fail, key: %s", key.c_str());
  }
}

void KVDB::multiGet(const vector<string> &keys, vector<string> &values) {
  values.clear();
  std::vector<rocksdb::Slice> keysSlice;
  for (const auto key : keys) {
    keysSlice.push_back(key);
  }

  // (*values) will always be resized to be the same size as (keys).
  db_->MultiGet(rocksdb::ReadOptions(), keysSlice, &values);
}

void KVDB::rangeGetGT(const string &key, const size_t limit, vector<vector<uint8_t> > &bufferVec) {}
void KVDB::rangeGetLT(const string &key, const size_t limit, vector<vector<uint8_t> > &bufferVec) {}

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
      keys.push_back(rocksdb::Slice(std::end(keysStr)->data(), std::end(keysStr)->size()));
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
