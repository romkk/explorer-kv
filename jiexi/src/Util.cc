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

int32_t HexToDecLast2Bytes(const string &hex) {
  if (hex.length() < 2) {
    return 0;
  }
  const string h = hex.substr(hex.length() - 2, 2);
  return (int32_t)HexDigit(h[0]) * 16 + (int32_t)HexDigit(h[1]);
}

int32_t AddressTableIndex(const string &address) {
  CKeyID key;
  if (!CBitcoinAddress(address).GetKeyID(key)) {
    return 0;
  }
  const string h = key.GetHex();
  // 输出是反序的
  // 例如：1Dhx3kGVkLaVFDYacZARheNzAWhYPTxHLq
  // 应该是：8b60195db4692837d7f61b7be8aa11ecdfaecdcf
  // 实际string h是：cfcdaedfec11aae87b1bf6d7372869b45d19608b
  // 所以取头部两个字符
  return HexToDecLast2Bytes(h.substr(0, 1) + h.substr(1, 1));
}

// 获取地址的ID映射关系
// 不存在则创建一条记录，该函数执行在DB事务之外，独立的，不受DB事务影响
// 由于自增ID是自行管理的，所以必须在DB事务之外
bool GetAddressIds(const string &dbURI, const set<string> &allAddresss,
                   map<string, int64_t> &addrMap) {
  MySQLConnection db(dbURI);
  const string now = date("%F %T");

  for (auto &a : allAddresss) {
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
      sql = Strings::Format("INSERT INTO `%s` (`id`,`address`, `tx_count`,"
                            " `total_received`, `total_sent`, `created_at`, `updated_at`)"
                            " VALUES("
                            "  (SELECT * FROM (SELECT IFNULL(MAX(`id`), %lld) + 1 FROM `%s`) AS t1)"
                            "  , '%s', 0, 0, 0, '%s', '%s')",
                            tableName.c_str(),
                            tableIdx * BILLION, tableName.c_str(),
                            a.c_str(), now.c_str(), now.c_str());
      if (db.update(sql) == 0) {
        return false;
      }
      db.query(sqlSelect, res);
    }

    assert(res.numRows() == 1);
    row = res.nextRow();
    addrMap.insert(std::make_pair(a, atoi64(row[0])));
  }
  return true;
}

