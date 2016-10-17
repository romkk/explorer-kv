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

#include "BlockImporter.h"

#include <boost/filesystem.hpp>
#include <boost/thread.hpp>

#include "utilities_js.hpp"

namespace fs = boost::filesystem;

inline bool _bitcoindRpcCall(const char *reqData, string &response) {
  return bitcoindRpcCall(Config::GConfig.get("bitcoind.url"    ).c_str(),
                         Config::GConfig.get("bitcoind.userpwd").c_str(),
                         reqData, response);
}

static bool _checkBitcoind() {
  string response;
  string request = "{\"jsonrpc\":\"1.0\",\"id\":\"1\",\"method\":\"getinfo\",\"params\":[]}";
  if (!_bitcoindRpcCall(request.c_str(), response)) {
    return false;
  }
  JsonNode r;
  if (!JsonNode::parse(response.c_str(),
                       response.c_str() + response.length(), r)) {
    return false;
  }
  // check fields
  if (r["result"].type() != Utilities::JS::type::Obj ||
      r["result"]["connections"].type() != Utilities::JS::type::Int ||
      r["result"]["blocks"].type()      != Utilities::JS::type::Int) {
    return false;
  }
  if (r["result"]["connections"].int32() <= 0) {
    return false;
  }

  return true;
}


static void _getblock(int32_t height, string **data);


BlockImporter::BlockImporter(const string &dir, const int32_t nProduceThreads,
                             const string &bitcoindUri,
                             int32_t beginHeight, int32_t endHeight):
running_(false), dir_(dir), nProduceThreads_(nProduceThreads), bitcoindUri_(bitcoindUri) {
  runningProduceThreads_ = 0;
  runningConsumeThreads_ = 0;

  // try create dir
  if (dir_.length() == 0) {
    dir_ = ".";
  }
  if (*std::prev(dir_.end()) != '/') {
    dir_ += "/";
  }
  fs::path fileDir(dir_);
  tryCreateDirectory(fileDir);

  // set height range
  assert(beginHeight <= endHeight);
  dataVec_.resize(endHeight - beginHeight + 1, nullptr);

  currHeight_  = beginHeight;
  beginHeight_ = beginHeight;
  endHeight_   = endHeight;
}

BlockImporter::~BlockImporter() {
  if (f_) { fclose(f_); }
}

void BlockImporter::stop() {
  LOG_INFO("BlockImporter::stop");
  running_ = false;
}

void BlockImporter::run() {
  running_ = true;

  // check bitcoind
  if (_checkBitcoind() == false) {
    THROW_EXCEPTION_DBEX("bitcoind is not working or error");
  }

  // open file
  const string file = Strings::Format("%s%d_%d", dir_.c_str(), beginHeight_, endHeight_);
  f_ = fopen(file.c_str(), "wa");
  if (f_ == nullptr) {
    THROW_EXCEPTION_DBEX("open file fail");
  }

  // 启动生成线程
  runningProduceThreads_ = nProduceThreads_;
  for (int i = 0; i < nProduceThreads_; i++) {
    boost::thread t(boost::bind(&BlockImporter::threadProduceBlock, this));
  }

  // 启动消费线程
  runningConsumeThreads_ = 1;  // 一个消费线程，需要保障写入是顺序的
  boost::thread t(boost::bind(&BlockImporter::threadConsumeBlock, this));

  // ...

  // 等待生成线程处理完成
  while (runningProduceThreads_ > 0 || runningConsumeThreads_ > 0) {
    sleep(1);
  }
}

void BlockImporter::writeDisk(const string *data, int32_t height) {
  // write height
  const string str = Strings::Format("%d\t%s\n", height, data->c_str());
  fwrite(str.c_str(), 1u, str.length(), f_);
}

void BlockImporter::threadConsumeBlock() {
  string *data = nullptr;

  for (auto i = 0; running_ && i < dataVec_.size(); ) {
    {
      ScopeLock sl(lock_);
      data = dataVec_[i];
      dataVec_[i] = nullptr;
    }
    if (data == nullptr) {
      sleepMs(200);
      continue;
    }

    const int32_t height = i + beginHeight_;
    writeDisk(data, height);
    delete data;
    LOG_INFO("consume block height: %d", height);

    i++;
  }

  runningConsumeThreads_--;
}

void BlockImporter::threadProduceBlock() {

  while (running_) {
    //  获取一个高度
    int32_t height = 0;
    {
      ScopeLock sl(lock_);
      height = currHeight_++;
    }
    if (height > endHeight_) {
      LOG_INFO("reach max height, thread exit");
      break;  // exit thread
    }

    // 防止失败，重试机制
    string *data = nullptr;
    for (auto i = 0; i < 5; i++) {
      try {
        _getblock(height, &data);
      } catch (std::exception & e) {
        LOG_WARN("_getblock exception: %s", e.what());
      }
      if (data != nullptr) { break; }
      LOG_WARN("fetch block fail: %d, try again", height);
      sleep(1);
    }

    if (data == nullptr) {
      THROW_EXCEPTION_DBEX("fetch block fail: %d", height);
    }

    {
      ScopeLock sl(lock_);
      dataVec_[height - beginHeight_] = data;
    }

    LOG_INFO("produce block height: %d", height);
  } /* /while */

  runningProduceThreads_--;
}

void _getblock(int32_t height, string **data) {
  string request, response;
  JsonNode r, result;

  //
  // 获取高度对应的Hash
  //
  request = Strings::Format("{\"id\":1,\"method\":\"getblockhash\",\"params\":[%d]}", height);

  // rpc call
  if (_bitcoindRpcCall(request.c_str(), response) == false) {
    THROW_EXCEPTION_DBEX("bitcoind rpc call fail, req: %s", request.c_str());
  }

  if (!JsonNode::parse(response.c_str(), response.c_str() + response.length(), r)) {
    THROW_EXCEPTION_DBEX("json parse failure: %s", response.c_str());
  }
  result = r["result"];
  if (result.type() != Utilities::JS::type::Str || result.str().length() != 64) {
    THROW_EXCEPTION_DBEX("getblockhash fail, response: %s", response.c_str());
  }
  const string blockhash = result.str();

  //
  // 获取 block raw hex
  //
  request = Strings::Format("{\"id\":2,\"method\":\"getblock\",\"params\":[\"%s\",false]}", blockhash.c_str());

  // rpc call
  if (_bitcoindRpcCall(request.c_str(), response) == false) {
    THROW_EXCEPTION_DBEX("bitcoind rpc call fail, req: %s", request.c_str());
  }

  if (!JsonNode::parse(response.c_str(), response.c_str() + response.length(), r)) {
    THROW_EXCEPTION_DBEX("json parse failure: %s", response.c_str());
  }
  result = r["result"];
  if (result.type() != Utilities::JS::type::Str) {
    THROW_EXCEPTION_DBEX("getblock fail, response: %s", response.c_str());
  }

  *data = new string(r["result"].str());
}
