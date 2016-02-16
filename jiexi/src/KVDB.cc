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

#include "rocksdb/cache.h"
#include "rocksdb/compaction_filter.h"
#include "rocksdb/db.h"
#include "rocksdb/options.h"
#include "rocksdb/slice.h"
#include "rocksdb/table.h"
#include "rocksdb/memtablerep.h"

//
// TODO: 采用列族(column family)来优化相同的key,
//       例如： 00_{tx_hash}, 01_{tx_hash}, 02_{tx_hash}
//


KVDB::KVDB(const string &dbPath): db_(nullptr), kDBPath_(dbPath) {
  options_.compression = rocksdb::kSnappyCompression;
  options_.create_if_missing        = false;
//  options_.disableDataSync          = true;   // disable syncing of data files

  //
  // open Rocks DB
  //
  // Optimize RocksDB. This is the easiest way to get RocksDB to perform well
  options_.IncreaseParallelism();
  options_.OptimizeLevelStyleCompaction();

  //
  // https://github.com/facebook/rocksdb/blob/master/tools/benchmark.sh
  //
  options_.target_file_size_base    = (size_t)128 * 1024 * 1024;
  options_.max_bytes_for_level_base = (size_t)1024 * 1024 * 1024;
  options_.num_levels = 6;

  options_.write_buffer_size = (size_t)128 * 1024 * 1024;
  options_.max_write_buffer_number  = 8;
//  options_.disable_auto_compactions = true;
//
//  // params_bulkload
//  options_.max_background_compactions = 16;
//  options_.max_background_flushes = 7;
//  options_.level0_file_num_compaction_trigger = (size_t)10 * 1024 * 1024;
//  options_.level0_slowdown_writes_trigger     = (size_t)10 * 1024 * 1024;
//  options_.level0_stop_writes_trigger         = (size_t)10 * 1024 * 1024;
//
//  // memtable_factory: vector
//  options_.memtable_factory.reset(new rocksdb::VectorRepFactory);

  // initialize BlockBasedTableOptions
  auto cache1 = rocksdb::NewLRUCache(1 * 1024 * 1024 * 1024LL);
  auto cache2 = rocksdb::NewLRUCache(1 * 1024 * 1024 * 1024LL);
  rocksdb::BlockBasedTableOptions bbt_opts;
  bbt_opts.block_cache = cache1;
  bbt_opts.block_cache_compressed = cache2;
  bbt_opts.cache_index_and_filter_blocks = 0;
  bbt_opts.block_size = 32 * 1024;
  bbt_opts.format_version = 2;
  options_.table_factory.reset(rocksdb::NewBlockBasedTableFactory(bbt_opts));
}

KVDB::~KVDB() {
  close();
}

void KVDB::close() {
  if (db_ != nullptr) {
    LOG_INFO("close kv db");
    delete db_;
    db_ = nullptr;
  }
}

void KVDB::status(string *s) {
  s->clear();
  std::string out;

  // how much memory is being used by index and filter blocks
  db_->GetProperty("rocksdb.estimate-table-readers-mem", &out);
  *s += "index/filter: " + out;
  out.clear();

  // memtable size
  db_->GetProperty("rocksdb.cur-size-all-mem-tables", &out);
  *s += ", memtable: " + out;
  out.clear();
}

void KVDB::open() {
  // open DB
  rocksdb::Status s = rocksdb::DB::Open(options_, kDBPath_, &db_);
  if (!s.ok()) {
    THROW_EXCEPTION_DBEX("open rocks db fail: %s", s.ToString().c_str());
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
  LOG_DEBUG("[KVDB::del] key: %s", key.c_str());
  rocksdb::Status s = db_->Delete(rocksdb::WriteOptions(), key);
  if (!s.ok()) {
    THROW_EXCEPTION_DBEX("delete key: %s fail", key.c_str());
  }
}

void KVDB::get(const string &key, string &value) {
  value.clear();
  rocksdb::Status s = db_->Get(rocksdb::ReadOptions(), key, &value);
  LOG_DEBUG("[KVDB::get] key: %s, value size: %llu", key.c_str(), value.size());
  if (s.IsNotFound()) {
    THROW_EXCEPTION_DBEX("not found key: %s", key.c_str());
  }
}

bool KVDB::getMayNotExist(const string &key, string &value) {
  value.clear();
  rocksdb::Status s = db_->Get(rocksdb::ReadOptions(), key, &value);
  LOG_DEBUG("[KVDB::getMayNotExist] key: %s, value size: %llu", key.c_str(), value.size());
  if (s.IsNotFound()) {
    return false;
  }
  return true;
}

void KVDB::set(const string &key, const string &value) {
  rocksdb::Slice sliceValue(value.data(), value.size());
  rocksdb::Status s = db_->Put(rocksdb::WriteOptions(), key, sliceValue);
  LOG_DEBUG("[KVDB::set] key: %s, value size: %llu", key.c_str(), value.size());
  if (!s.ok()) {
    THROW_EXCEPTION_DBEX("set key fail, key: %s", key.c_str());
  }
}

void KVDB::set(const string &key, const uint8_t *data, const size_t length) {
  rocksdb::Slice value((const char *)data, length);
  rocksdb::Status s = db_->Put(rocksdb::WriteOptions(), key, value);
  LOG_DEBUG("[KVDB::set] key: %s, value size: %llu", key.c_str(), length);
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
                 vector<string> &keys, vector<string> &values, int32_t offset) {
  keys.clear();
  values.clear();

  if (offset < 0) { offset = 0; }

  if (strcmp(start.c_str(), end.c_str()) <= 0) {
    rangeGT(start, end, limit, keys, values, offset);
  } else {
    rangeLT(start, end, limit, keys, values, offset);
  }
}

// [start, end]: start < end
void KVDB::rangeGT(const string &start, const string &end, const int32_t limit,
                   vector<string> &keys, vector<string> &values, int32_t offset) {
  assert(strcmp(start.c_str(), end.c_str()) <= 0);
  LOG_DEBUG("rangeGT, start: %s, end: %s, limit: %d, offset: %d",
            start.c_str(), end.c_str(), limit, offset);

  int32_t cnt = 0;
  auto it = db_->NewIterator(rocksdb::ReadOptions());
  for (it->Seek(start);
       it->Valid() && strcmp(it->key().ToString().c_str(), end.c_str()) <= 0;
       it->Next())
  {
    // skip offset
    cnt++;
    if (offset && offset > (cnt - 1)) {
      continue;
    }

    keys.push_back(it->key().ToString());
    values.push_back(std::string(it->value().data(), it->value().size()));
    // 检测数量
    if (keys.size() >= limit) { break; }
  }
}

// [start, end]: start > end
void KVDB::rangeLT(const string &start, const string &end, const int32_t limit,
                   vector<string> &keys, vector<string> &values, int32_t offset) {
  assert(strcmp(start.c_str(), end.c_str()) >= 0);
  LOG_DEBUG("rangeLT, start: %s, end: %s, limit: %d, offset: %d",
            start.c_str(), end.c_str(), limit, offset);

  int32_t cnt = 0;
  auto it = db_->NewIterator(rocksdb::ReadOptions());
  it->Seek(start);

  // 因为是逆向的，所以检测第一个，Seek()的当前key是大于等于stat的
  if (it->key().ToString() != start) {
    it->Prev();
  }
  for (;
       it->Valid() && strcmp(it->key().ToString().c_str(), end.c_str()) >= 0;
       it->Prev())
  {
    // skip offset
    cnt++;
    if (offset && offset > (cnt - 1)) {
      continue;
    }

    keys.push_back(it->key().ToString());
    values.push_back(std::string(it->value().data(), it->value().size()));
    // 检测数量
    if (keys.size() >= limit) { break; }
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
