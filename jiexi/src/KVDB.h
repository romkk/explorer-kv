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

#ifndef KVDB_H_
#define KVDB_H_

#include "bitcoin/core.h"
#include "bitcoin/key.h"

#include "rocksdb/db.h"
#include "rocksdb/slice.h"
#include "rocksdb/options.h"

#include "Common.h"

#include "explorer_generated.h"

#define KVDB_PREFIX_TX_RAW_HEX   "00_"
#define KVDB_PREFIX_TX_OBJECT    "01_"
#define KVDB_PREFIX_TX_SPEND     "02_"

#define KVDB_PREFIX_BLOCK_HEIGHT     "10_"
#define KVDB_PREFIX_BLOCK_OBJECT     "11_"
#define KVDB_PREFIX_BLOCK_TXS_STR    "12_"

#define KVDB_PREFIX_ADDR_OBJECT     "20_"
#define KVDB_PREFIX_ADDR_TX         "21_"
#define KVDB_PREFIX_ADDR_TX_INDEX   "22_"
#define KVDB_PREFIX_ADDR_UNSPENT    "23_"
#define KVDB_PREFIX_ADDR_UNSPENT_INDEX  "24_"


class KVDB {
  rocksdb::Options options_;

  string kDBPath_;

public:
  rocksdb::DB *db_;

  KVDB(const string &dbPath);
  ~KVDB() {}

  void open();

  bool keyExist(const string &key);
  void del(const string &key);
  void get(const string &key, string &value);
  void set(const string &key, const string &value);
  void set(const string &key, const vector<uint8_t> &buffer);
  void set(const string &key, const uint8_t *data, const size_t length);

  void multiGet(const vector<string> &keys, vector<string> &bufferVec);
  void multiSet(const vector<string> &keys, const vector<string> &bufferVec);

  void rangeGetGT(const string &key, const size_t limit, vector<vector<uint8_t> > &bufferVec);
  void rangeGetLT(const string &key, const size_t limit, vector<vector<uint8_t> > &bufferVec);

  void getPrevTxOutputs(const CTransaction &tx,
                        vector<string> &prevTxsData,
                        vector<const fbe::TxOutput *> &prevTxOutputs);
};

#endif
