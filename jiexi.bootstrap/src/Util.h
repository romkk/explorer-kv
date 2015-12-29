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

#ifndef Explorer_Util_h
#define Explorer_Util_h

#include <string>
#include <sstream>
#include <vector>

#include <boost/circular_buffer.hpp>

#include "Common.h"
#include "MySQLConnection.h"

#include "bitcoin/base58.h"
#include "bitcoin/core.h"
#include "bitcoin/util.h"

#define BILLION 1000000000  // 10亿, 10^9

vector<string> split(const string &s, const char delim);
vector<string> split(const string &s, const char delim, const int32_t limit);

size_t getNumberOfLines(const string &file);

string EncodeHexTx(const CTransaction &tx);
string EncodeHexBlock(const CBlock &block);

bool DecodeHexTx(CTransaction& tx, const std::string& strHexTx);
bool DecodeHexBlk(CBlock& block, const std::string& strHexBlk);


////////////////////////////////  BoundedBuffer  ////////////////////////////////
template <class T>
class BoundedBuffer {
  typedef boost::circular_buffer<T> container_type;
  typedef typename container_type::size_type size_type;
  typedef typename container_type::value_type value_type;

  container_type container_;
  size_type unread_;
  std::mutex lock_;

  std::condition_variable notFull_;
  std::condition_variable notEmpty_;

public:
  BoundedBuffer(size_type capacity) : container_(capacity), unread_(0) {}
  ~BoundedBuffer() {}

  void pushFront(const T &item){
    std::unique_lock<std::mutex> l(lock_);
    notFull_.wait(l, [this](){ return unread_ < container_.capacity(); });

    container_.push_front(item);
    ++unread_;
    lock_.unlock();
    notEmpty_.notify_one();
  }

  void popBack(value_type *pItem) {
    std::unique_lock<std::mutex> l(lock_);
    notEmpty_.wait(l, [this](){ return unread_ > 0; });

    *pItem = container_[--unread_];
    lock_.unlock();
    notFull_.notify_one();
  }
};

#endif
