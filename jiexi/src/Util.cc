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
  return HexToDecLast2Bytes(key.GetHex());
}


// success: return > 0, failure: =0
int64_t GetMaxAddressId(MySQLConnection &db) {
  MySQLResult res;
  char **row;
  int64_t maxId = 1;
  string sql;
  const string now = date("%F %T");

  sql = "SELECT `value` FROM `0_explorer_meta` WHERE `key`='jiexi.max_address_id' ";
  db.query(sql, res);
  if (res.numRows()) {
    row = res.nextRow();
    maxId = atoi64(row[0]);
  } else {
    sql = Strings::Format("INSERT INTO `0_explorer_meta` (`key`, `value`, `created_at`, `updated_at`) "
                          " VALUES('jiexi.max_address_id', '1', '%s', '%s')",
                          now.c_str(), now.c_str());
    if (db.update(sql) != 1) {
      return 0;
    }
  }

  // 增一
  sql = Strings::Format("UPDATE `0_explorer_meta` SET `value`='%lld', `updated_at`='%s' "
                        " WHERE `key`='jiexi.max_address_id' AND `value`='%lld' ",
                        maxId + 1, now.c_str(), maxId);
  if (db.update(sql) == 1) {
    return maxId;
  }
  return 0;
}

// 获取地址的ID映射关系
// 不存在则创建一条记录，该函数执行在存储过程之外，独立的，不受DB事务影响
bool GetAddressIds(const string &dbURI, const set<string> &allAddresss,
                   map<string, int64_t> addrMap) {
  MySQLConnection db(dbURI);
  const string now = date("%F %T");

  for (auto &a : allAddresss) {
    MySQLResult res;
    char **row;
    const string tableName = Strings::Format("addresses_%04d", AddressTableIndex(a));
    string sql = Strings::Format("SELECT `id` FROM `%s` WHERE `address`='%s' ",
                                 tableName.c_str(), a.c_str());
    db.query(sql, res);
    if (res.numRows() == 0) {
      int64_t nextAddrId = GetMaxAddressId(db);

      // 地址不存在，创建一条记录
      string sql2 = Strings::Format("INSERT INTO `%s` (`id`,`address`, `tx_count`,"
                                    " `total_received`, `total_sent`, `created_at`, `updated_at`)"
                                    " VALUES(%lld, '%s', 0, 0, 0, '%s', '%s')",
                                    tableName.c_str(), nextAddrId, a.c_str(),
                                    now.c_str(), now.c_str());
    }
  }
  return false;
}
