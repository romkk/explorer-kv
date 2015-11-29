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
  
  // 采用 range 方式获取数据最多为 N
  kRangeMaxSize_ = 10000;
}

KVDB::~KVDB() {
  close();
}

void KVDB::close() {
  if (db_ != nullptr) {
    delete db_;
    db_ = nullptr;
  }
}

void KVDB::open() {
  // open DB
  rocksdb::Status s = rocksdb::DB::Open(options_, kDBPath_, &db_);
  if (!s.ok()) {
    THROW_EXCEPTION_DBEX("open rocks db fail");
  }
}

//bool KVDB::keyExist(const string &key) {
//  string value;
//  // If the key definitely does not exist in the database, then this method
//  // returns false, else true.
//  if (db_->KeyMayExist(rocksdb::ReadOptions(), key, &value)) {
//    return true;
//  }
//  return false;
//}

void KVDB::del(const string &key) {
  rocksdb::Status s = db_->Delete(rocksdb::WriteOptions(), key);
  if (!s.ok()) {
    THROW_EXCEPTION_DBEX("delelte key: %s fail", key.c_str());
  }
}

void KVDB::get(const string &key, string &value) {
  value.clear();
  rocksdb::Status s = db_->Get(rocksdb::ReadOptions(), key, &value);
  if (s.IsNotFound()) {
    THROW_EXCEPTION_DBEX("not found key: %s", key.c_str());
  }
  LOG_DEBUG("[KVDB::get] key: %s, value size: %llu", key.c_str(), value.size());
}

void KVDB::getMayNotExist(const string &key, string &value) {
  value.clear();
  db_->Get(rocksdb::ReadOptions(), key, &value);
  LOG_DEBUG("[KVDB::getMayNotExist] key: %s, value size: %llu", key.c_str(), value.size());
}

void KVDB::set(const string &key, const string &value) {
  LOG_DEBUG("[KVDB::set] key: %s, value size: %llu", key.c_str(), value.size());
  rocksdb::Slice sliceValue(value.data(), value.size());
  rocksdb::Status s = db_->Put(rocksdb::WriteOptions(), key, sliceValue);
  if (!s.ok()) {
    THROW_EXCEPTION_DBEX("set key fail, key: %s", key.c_str());
  }
}

void KVDB::set(const string &key, const uint8_t *data, const size_t length) {
  LOG_DEBUG("[KVDB::set] key: %s, value size: %llu", key.c_str(), length);
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

// [start, end]
void KVDB::range(const string &start, const string &end, const int32_t limit,
                 vector<string> &keys, vector<string> &values) {
  keys.clear();
  values.clear();
  
  if (strcmp(start.c_str(), end.c_str()) <= 0) {
    rangeGT(start, end, limit > 0 ? limit : kRangeMaxSize_, keys, values);
  } else {
    rangeLT(start, end, limit > 0 ? limit : kRangeMaxSize_, keys, values);
  }
}

// [start, end]: start < end
void KVDB::rangeGT(const string &start, const string &end, const int32_t limit,
                   vector<string> &keys, vector<string> &values) {
  assert(strcmp(start.c_str(), end.c_str()) <= 0);
  LOG_DEBUG("rangeGT, start: %s, end: %s, limit: %d", start.c_str(), end.c_str(), limit);
  
  auto it = db_->NewIterator(rocksdb::ReadOptions());
  for (it->Seek(start);
       it->Valid() && strcmp(it->key().ToString().c_str(), end.c_str()) <= 0;
       it->Next()) {
    keys.push_back(it->key().ToString());
    values.push_back(std::string(it->value().data(), it->value().size()));
    
    // 检测数量
    if (keys.size() >= limit || keys.size() > kRangeMaxSize_ /* MAX */) { break; }
  }
}

// [start, end]: start > end
void KVDB::rangeLT(const string &start, const string &end, const int32_t limit,
                   vector<string> &keys, vector<string> &values) {
  assert(strcmp(start.c_str(), end.c_str()) >= 0);
  LOG_DEBUG("rangeLT, start: %s, end: %s, limit: %d", start.c_str(), end.c_str(), limit);
  
  auto it = db_->NewIterator(rocksdb::ReadOptions());
  for (it->Seek(start);
       it->Valid() && strcmp(it->key().ToString().c_str(), end.c_str()) >= 0;
       it->Prev()) {
    keys.push_back(it->key().ToString());
    values.push_back(std::string(it->value().data(), it->value().size()));
    
    // 检测数量
    if (keys.size() >= limit || keys.size() > kRangeMaxSize_ /* MAX */) { break; }
  }
}

void KVDB::getPrevTxOutputs(const CTransaction &tx,
                            vector<string> &prevTxsData,
                            vector<const fbe::TxOutput *> &prevTxOutputs) {
  if (tx.IsCoinBase()) { return; }

  std::map<string /*tx hash*/, const fbe::Tx *> prevTxs;
  vector<string> keys;

  {
    std::set<uint256> prevTxHashSet;
    for (const auto &input : tx.vin) {
      if (prevTxHashSet.count(input.prevout.hash) != 0) {
        continue;
      }
      prevTxHashSet.insert(input.prevout.hash);

      // 01_{tx_hash}
      keys.push_back(Strings::Format("%s%s", KVDB_PREFIX_TX_OBJECT,
                                     input.prevout.hash.ToString().c_str()));
    }
  }
  multiGet(keys, prevTxsData);

  for (size_t i = 0; i < keys.size(); i++) {
    const fbe::Tx *prevTx = flatbuffers::GetRoot<fbe::Tx>(prevTxsData[i].data());
    // key有前缀，移除之，得到 tx_hash
    prevTxs.insert(std::make_pair(keys[i].substr(3), prevTx));
  }
//
//  for (auto it : prevTxs) {
//    std::cout << it.first << std::endl;
//  }

  for (const auto &input : tx.vin) {
    const uint256 &phash = input.prevout.hash;
    const uint32_t &ppos = input.prevout.n;
//    assert(prevTxs.find(phash.ToString()) != prevTxs.end());
//    std::cout << ppos << ": " << phash.ToString() << std::endl;
    const auto output = prevTxs[phash.ToString()]->outputs()->operator[](ppos);
    prevTxOutputs.push_back(output);
  }

  assert(tx.vin.size() == prevTxOutputs.size());
}
