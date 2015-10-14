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

#include "Common.h"
#include "MySQLConnection.h"

#include "bitcoin/base58.h"
#include "bitcoin/core.h"
#include "bitcoin/util.h"

#define BILLION 1000000000  // 10亿, 10^9

std::vector<std::string> split(const std::string &s, const char delim);
std::vector<std::string> split(const std::string &s, const char delim, const int32_t limit);
std::string implode(const std::vector<std::string> &arr, const std::string &glue);

int32_t HexToDecLast2Bytes(const string &hex);
int32_t AddressTableIndex(const string &address);

void GetAddressIds(MySQLConnection &db, const set<string> &allAddresss,
                   map<string, int64_t> &addrMap);
int64_t txHash2Id(MySQLConnection &db, const uint256 &txHash);
string getTxHexByHash(MySQLConnection &db, const uint256 &txHash);

int64_t insertRawBlock(MySQLConnection &db, const CBlock &blk, const int32_t height);
int64_t insertRawTx(MySQLConnection &db, const CTransaction &tx);

void callBlockRelayParseUrl(const string &blockHash);

string EncodeHexTx(const CTransaction &tx);
string EncodeHexBlock(const CBlock &block);

bool DecodeHexTx(CTransaction& tx, const std::string& strHexTx);
bool DecodeHexBlk(CBlock& block, const std::string& strHexBlk);

#endif
