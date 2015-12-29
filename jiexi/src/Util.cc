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

#include <iomanip>
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

std::string implode(const std::vector<std::string> &arr, const std::string &glue) {
  string s;
  if (arr.size() == 0) {
    return s;
  }
  for (const auto &it : arr) {
    s += it + glue;
  }
  s.resize(s.length() - glue.length());  // remove last glue
  return s;
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
  char **row = nullptr;
  const uint256 blockHash = blk.GetHash();

  // check is exist
  sql = Strings::Format("SELECT `id`,`block_hash` FROM `0_raw_blocks` "
                        " WHERE `block_hash` = '%s' ",
                        blockHash.ToString().c_str());
  db.query(sql, res);
  if (res.numRows() == 1) {
    row = res.nextRow();
    assert(atoi(row[1]) == height);
    return atoi64(row[0]);
  }

  //
  // update chain_id, 从最大的 chain_id 记录向上递增
  // 0_raw_blocks.chain_id 与 0_blocks.chain_id 同一个块的 chain_id 不一定一致
  //
  sql = Strings::Format("SELECT `id` FROM `0_raw_blocks` "
                        " WHERE `block_height` = %lld ORDER BY `chain_id` DESC ",
                        height);
  db.query(sql, res);
  while ((row = res.nextRow()) != nullptr) {
    const string sql2 = Strings::Format("UPDATE `0_raw_blocks` SET `chain_id` = `chain_id` + 1"
                                        " WHERE `id` = %lld ", atoi64(row[0]));
    db.updateOrThrowEx(sql2, 1);
  }

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

  // TODO: 将最近使用的 txhash 进行缓存，有利于当新块到来时，避免执行大量 select SQL语句
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
                        " '%s', '%s', '%s');",
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


/* Converts a hex character to its integer value */
static char from_hex(char ch) {
  return isdigit(ch) ? ch - '0' : tolower(ch) - 'a' + 10;
}

/* Converts an integer value to its hex character*/
static char to_hex(char code) {
  static char hex[] = "0123456789ABCDEF";
  return hex[code & 0xFU];
}

/* Returns a url-encoded version of str */
// URL-encode according to RFC 3986
// unreserved = ALPHA / DIGIT / "-" / "." / "_" / "~"
string UrlEncode(const char *str) {
  string buf;
  buf.resize(strlen(str) * 3 + 1, 0);
  const char *pstr = str;
  char *pbuf = &buf[0];
  while (*pstr) {
    if (isalnum(*pstr) || *pstr == '-' || *pstr == '_' || *pstr == '.' || *pstr == '~') {
      *pbuf++ = *pstr;
    } else {
      *pbuf++ = '%', *pbuf++ = to_hex(*pstr >> 4), *pbuf++ = to_hex(*pstr & 0xFU);
    }
    pstr++;
  }
  *pbuf = '\0';
  buf.resize(strlen(buf.c_str()));
  return buf;
}

/* Returns a url-decoded version of str */
//
// URL-encode according to RFC 3986
// unreserved = ALPHA / DIGIT / "-" / "." / "_" / "~"
// 额外添加 '=' -> ' '
//
string UrlDecode(const char *str) {
  string buf;
  buf.resize(strlen(str) + 1, 0);
  const char *pstr = str;
  char *pbuf = &buf[0];
  while (*pstr) {
    if (*pstr == '%') {
      if (pstr[1] && pstr[2]) {
        *pbuf++ = from_hex(pstr[1]) << 4 | from_hex(pstr[2]);
        pstr += 2;
      }
    } else if (*pstr == '+') {
      *pbuf++ = ' ';
    } else {
      *pbuf++ = *pstr;
    }
    pstr++;
  }
  *pbuf = '\0';
  buf.resize(strlen(buf.c_str()));
  return buf;
}

std::string escapeJson(const std::string &s) {
  std::ostringstream o;
  for (auto c = s.cbegin(); c != s.cend(); c++) {
    switch (*c) {
      case '"' : o << "\\\""; break;
      case '\\': o << "\\\\"; break;
      case '\b': o << "\\b"; break;
      case '\f': o << "\\f"; break;
      case '\n': o << "\\n"; break;
      case '\r': o << "\\r"; break;
      case '\t': o << "\\t"; break;
      default:
        if ('\x00' <= *c && *c <= '\x1f') {
          o << "\\u"
          << std::hex << std::setw(4) << std::setfill('0') << (int)*c;
        } else {
          o << *c;
        }
    }
  }
  return o.str();
}
