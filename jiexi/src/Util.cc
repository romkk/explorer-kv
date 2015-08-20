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

#include "Util.h"
#include "MySQLConnection.h"

#include <string>
#include <sstream>
#include <vector>

#include <boost/algorithm/string/replace.hpp>
#include <boost/thread.hpp>

static std::vector<std::string> &split(const std::string &s, const char delim,
                                       std::vector<std::string> &elems,
                                       const int32_t limit) {
  std::stringstream ss(s);
  std::string item;
  while (std::getline(ss, item, delim)) {
    elems.push_back(item);
    if (limit != -1 && elems.size() == limit) {
      break;
    }
  }
  return elems;
}

std::vector<std::string> split(const std::string &s, const char delim) {
  std::vector<std::string> elems;
  split(s, delim, elems, -1/* unlimit */);
  return elems;
}

std::vector<std::string> split(const std::string &s, const char delim,
                               const int32_t limit) {
  std::vector<std::string> elems;
  split(s, delim, elems, limit);
  return elems;
}

int32_t HexToDecLast2Bytes(const string &hex) {
  if (hex.length() < 2) {
    return 0;
  }
  const string h = hex.substr(hex.length() - 2, 2);
  return (int32_t)HexDigit(h[0]) * 16 + (int32_t)HexDigit(h[1]);
}

int32_t AddressTableIndex(const string &address) {
  CTxDestination dest;
  CBitcoinAddress addr(address);
  string h;
  if (!addr.IsValid()) {
    THROW_EXCEPTION_DBEX("invalid address: %s", address.c_str());
  }
  dest = addr.Get();

  if (addr.IsScript()) {
    h = boost::get<CScriptID>(dest).GetHex();
  } else {
    h = boost::get<CKeyID>(dest).GetHex();
  }

  // 输出是反序的
  // 例如：1Dhx3kGVkLaVFDYacZARheNzAWhYPTxHLq
  // 应该是：8b60195db4692837d7f61b7be8aa11ecdfaecdcf
  // 实际string h是：cfcdaedfec11aae87b1bf6d7372869b45d19608b
  // 所以取头部两个字符
  return HexToDecLast2Bytes(h.substr(0, 1) + h.substr(1, 1));
}

// 获取地址的ID映射关系
void GetAddressIds(MySQLConnection &db, const set<string> &allAddresss,
                   map<string, int64_t> &addrMap) {
  static std::unordered_map<string, int64_t> addrMapCache;
  const string now = date("%F %T");
  const bool isUseCache = Config::GConfig.getBool("cache.address2ids", false);

  for (auto &a : allAddresss) {
    if (addrMap.find(a) != addrMap.end()) {
      continue;  // already in map
    }

    // check if in cache
    if (isUseCache && addrMapCache.find(a) != addrMapCache.end()) {
      addrMap.insert(std::make_pair(a, addrMapCache[a]));
      continue;  // already in cache
    }

    MySQLResult res;
    char **row = nullptr;
    const int64_t tableIdx = AddressTableIndex(a) % 64;
    const string tableName = Strings::Format("addresses_%04d", tableIdx);
    const string sqlSelect = Strings::Format("SELECT `id` FROM `%s` WHERE `address`='%s' ",
                                             tableName.c_str(), a.c_str());
    string sql;

    db.query(sqlSelect, res);
    if (res.numRows() == 0) {
      // 地址不存在，创建一条记录
      // address ID: 单个表由区间，区间步进值为10亿，内部通过Mysql完成自增
      // SQL：里面需要弄一个临时表，否则报错：
      // You can't specify target table 'addresses_xxxx' for update in FROM clause
      sql = Strings::Format("INSERT INTO `%s` (`id`, `address`, `tx_count`,"
                            " `total_received`, `total_sent`, `created_at`, `updated_at`)"
                            " VALUES("
                            "  (SELECT * FROM (SELECT IFNULL(MAX(`id`), %lld) + 1 FROM `%s`) AS t1), "
                            " '%s', 0, 0, 0, '%s', '%s')",
                            tableName.c_str(), tableIdx * BILLION, tableName.c_str(),
                            a.c_str(), now.c_str(), now.c_str());
      db.updateOrThrowEx(sql, 1);
      db.query(sqlSelect, res);
    }

    assert(res.numRows() == 1);
    row = res.nextRow();

    addrMap.insert(std::make_pair(a, atoi64(row[0])));
    if (isUseCache) {
      if (addrMapCache.size() > 50 * 10000) {  // 最大缓存50万个地址，超过则清空
        std::unordered_map<string, int64_t>().swap(addrMapCache);
      }
      addrMapCache.insert(std::make_pair(a, atoi64(row[0])));
    }
  } /* /for */
}

int64_t txHash2Id(MySQLConnection &db, const uint256 &txHash) {
  MySQLResult res;
  char **row;
  string sql;

  const string hashStr = txHash.ToString();
  sql = Strings::Format("SELECT `id` FROM `raw_txs_%04d` WHERE `tx_hash`='%s'",
                        HexToDecLast2Bytes(hashStr) % 64, hashStr.c_str());
  db.query(sql, res);
  if (res.numRows() != 1) {
    THROW_EXCEPTION_DBEX("can't find rawtx: %s", hashStr.c_str());
  }
  row = res.nextRow();

  return atoi64(row[0]);
}

string getTxHexByHash(MySQLConnection &db, const uint256 &txHash) {
  MySQLResult res;
  char **row;
  string sql;

  const string hashStr = txHash.ToString();
  sql = Strings::Format("SELECT `hex` FROM `raw_txs_%04d` WHERE `tx_hash`='%s'",
                        HexToDecLast2Bytes(hashStr) % 64, hashStr.c_str());
  db.query(sql, res);
  if (res.numRows() != 1) {
    THROW_EXCEPTION_DBEX("can't find rawtx: %s", hashStr.c_str());
  }
  row = res.nextRow();
  return string(row[0]);
}

int64_t insertRawBlock(MySQLConnection &db, const CBlock &blk, const int32_t height) {
  string sql;
  MySQLResult res;
  const uint256 blockHash = blk.GetHash();

  // check is exist
  sql = Strings::Format("SELECT `id`,`block_hash` FROM `0_raw_blocks` "
                        " WHERE `block_hash` = '%s' ",
                        blockHash.ToString().c_str());
  db.query(sql, res);
  if (res.numRows() == 1) {
    char **row = res.nextRow();
    assert(atoi(row[1]) == height);
    return atoi64(row[0]);
  }

  // update chain_id
  sql = Strings::Format("UPDATE `0_raw_blocks` SET `chain_id` = `chain_id` + 1 "
                        " WHERE `block_height` = %lld ",
                        height);
  db.execute(sql.c_str());

  // insert raw
  const string blockHex = EncodeHexBlock(blk);
  sql = Strings::Format("INSERT INTO `0_raw_blocks` (`block_hash`, `block_height`,"
                        " `chain_id`, `hex`, `created_at`)"
                        " VALUES ('%s', %d, '0', '%s', '%s')",
                        blockHash.ToString().c_str(),
                        height, blockHex.c_str(), date("%F %T").c_str());
  db.updateOrThrowEx(sql, 1);

  return (int64_t)db.getInsertId();
}

int64_t insertRawTx(MySQLConnection &db, const CTransaction &tx) {
  string sql;
  MySQLResult res;
  const uint256 txHash = tx.GetHash();
  const string  txHashStr = txHash.ToString();

  // check is exist
  sql = Strings::Format("SELECT `id` FROM `raw_txs_%04d` WHERE `tx_hash`='%s'",
                        HexToDecLast2Bytes(txHashStr) % 64, txHashStr.c_str());
  db.query(sql, res);
  if (res.numRows() == 1) {
    char **row = res.nextRow();
    return atoi64(row[0]);
  }

  // insert raw
  const string txHex = EncodeHexTx(tx);
  const string tableName = Strings::Format("raw_txs_%04d", HexToDecLast2Bytes(txHashStr) % 64);
  sql = Strings::Format("INSERT INTO `%s` (`id`, `tx_hash`, `hex`, `created_at`) "
                        " VALUES ( "
                        " (SELECT IFNULL(MAX(`id`), 0) + 1 FROM `%s` as t1), "
                        ", '%s', '%s', '%s');",
                        tableName.c_str(), tableName.c_str(),
                        txHashStr.c_str(), txHex.c_str(), date("%F %T").c_str());
  db.updateOrThrowEx(sql, 1);

  return (int64_t)db.getInsertId();
}


// 回调块解析URL
void callBlockRelayParseUrl(const string &blockHash) {
  string url = Config::GConfig.get("block.relay.parse.url", "");
  if (url.length() == 0) { return; }

  boost::replace_all(url, "%s", blockHash);

  LOG_INFO("call block relay parse url: %s", url.c_str());
  boost::thread t(curlCallUrl, url); // thread runs free
}

string EncodeHexTx(const CTransaction& tx) {
  CDataStream ssTx(SER_NETWORK, BITCOIN_PROTOCOL_VERSION);
  ssTx << tx;
  return HexStr(ssTx.begin(), ssTx.end());
}

string EncodeHexBlock(const CBlock &block) {
  CDataStream ssBlock(SER_NETWORK, BITCOIN_PROTOCOL_VERSION);
  ssBlock << block;
  return HexStr(ssBlock.begin(), ssBlock.end());
}

bool DecodeHexTx(CTransaction& tx, const std::string& strHexTx)
{
  if (!IsHex(strHexTx))
    return false;

  vector<unsigned char> txData(ParseHex(strHexTx));
  CDataStream ssData(txData, SER_NETWORK, BITCOIN_PROTOCOL_VERSION);
  try {
    ssData >> tx;
  }
  catch (const std::exception &) {
    return false;
  }

  return true;
}

bool DecodeHexBlk(CBlock& block, const std::string& strHexBlk)
{
  if (!IsHex(strHexBlk))
    return false;

  std::vector<unsigned char> blockData(ParseHex(strHexBlk));
  CDataStream ssBlock(blockData, SER_NETWORK, BITCOIN_PROTOCOL_VERSION);
  try {
    ssBlock >> block;
  }
  catch (const std::exception &) {
    return false;
  }

  return true;
}