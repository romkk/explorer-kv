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
#include <iostream>
#include <fstream>

#include <pthread.h>

#include <boost/thread.hpp>

#include "Parser.h"
#include "Common.h"
#include "Util.h"

#include "bitcoin/base58.h"
#include "bitcoin/util.h"

static void _getRawTxFromDisk(const uint256 &hash, const int32_t height,
                              string *hex, int64_t *txId);

RawBlock::RawBlock(const int64_t blockId, const int32_t height, const int32_t chainId,
                   const uint256 hash, const char *hex) {
  blockId_ = blockId;
  height_  = height;
  chainId_ = chainId;
  hash_    = hash;
  hex_     = strdup(hex);
}
RawBlock::~RawBlock() {
  free(hex_);
}

// 从磁盘读取 raw_block 批量文件
// 0_raw_blocks文件里，高度依次递增
void _loadRawBlockFromDisk(map<int32_t, RawBlock*> &blkCache, const int32_t height) {
  // static vars
  static size_t lastOffset  = 0;
  static size_t lastHeight2 = 0;

  const int32_t KCountPerFile = 10000;  // 每个 0_raw_blocks 的容量
  string dir = Config::GConfig.get("rawdata.dir", "");
  // 尾部添加 '/'
  if (dir.length() == 0) {
    dir = "./";
  }
  else if (dir[dir.length()-1] != '/') {
    dir += "/";
  }

  const int32_t height2 = (height / KCountPerFile) * KCountPerFile;
  if (lastHeight2 != height2) {
    lastOffset = 0;
    lastHeight2 = height2;
  }

  string path = Strings::Format("%d_%d", height2, height2 + (KCountPerFile - 1));

  const string fname = Strings::Format("%s%s/0_raw_blocks",
                                       dir.c_str(), path.c_str());
  LOG_INFO("load raw block file: %s", fname.c_str());
  std::ifstream input(fname);
  if (lastOffset > 0) {
    input.seekg(lastOffset, input.beg);
  }
  std::string line;
  const size_t maxReadSize = 500 * 1024 * 1024;  // max read file

  while (std::getline(input, line)) {
    std::vector<std::string> arr = split(line, ',');
    // line: blockId, hash, height, chain_id, hex
    const uint256 blkHash(arr[1]);
    const int32_t blkHeight  = atoi(arr[2].c_str());
    const int32_t blkChainId = atoi(arr[3].c_str());

    blkCache[blkHeight] = new RawBlock(atoi64(arr[0].c_str()), blkHeight, blkChainId, blkHash, arr[4].c_str());

    if (input.tellg() > lastOffset + maxReadSize) {
      lastOffset = input.tellg();
    }
  }
}

// 从文件读取raw block
void getRawBlockFromDisk(const int32_t height, string *rawHex,
                          int32_t *chainId, int64_t *blockId) {
  // 从磁盘直接读取文件，缓存起来，减少数据库交互
  static map<int32_t, RawBlock*> blkCache;

  map<int32_t, RawBlock*>::const_iterator it = blkCache.find(height);

  if (it == blkCache.end()) {
    // clear data before reload
    for (auto &it2 : blkCache) {
      delete it2.second;
    }
    map<int32_t, RawBlock*>().swap(blkCache);  // clear

    // 载入数据
    LOG_INFO("try load raw block data from disk...");
    _loadRawBlockFromDisk(blkCache, height);

    it = blkCache.find(height);  // refind
  }

  if (it == blkCache.end()) {
    THROW_EXCEPTION_DBEX("can't find rawblock from disk cache, height: %d", height);
  }
  if (rawHex != nullptr)
    *rawHex  = it->second->hex_;
  if (chainId != nullptr)
    *chainId = it->second->chainId_;
  if (rawHex != nullptr)
    *blockId = it->second->blockId_;
}


