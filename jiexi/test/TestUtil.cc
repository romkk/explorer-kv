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

#include "gtest/gtest.h"
#include "Util.h"
#include "MySQLConnection.h"

TEST(Util, HexToDecLast2Bytes) {
  ASSERT_EQ(HexToDecLast2Bytes(""), 0);
  ASSERT_EQ(HexToDecLast2Bytes("f"), 0);
  ASSERT_EQ(HexToDecLast2Bytes("b60195db4692837d7f61b7be8aa11ecdfaecdcf"), 207);
  ASSERT_EQ(HexToDecLast2Bytes("8b60195db4692837d7f61b7be8aa11ecdfaecdcf"), 207);
}

TEST(Util, GetMaxAddressId) {
  const string uri = Config::GConfig.get("testdb.uri", "");
  if (uri.empty()) {
    LOG_WARN("skipped, test: [Util][GetMaxAddressId]");
    return;
  }

  MySQLConnection db(uri);
  {
    // drop table
    string sql = "DROP TABLE IF EXISTS `0_explorer_meta`;";
    db.update(sql);
  }

  {
    // create table
    string sql = "CREATE TABLE `0_explorer_meta` ("
    "`id` int(11) NOT NULL,"
    "`key` varchar(64) NOT NULL,"
    "`value` varchar(1024) NOT NULL,"
    "`created_at` datetime NOT NULL,"
    "`updated_at` datetime NOT NULL,"
    "PRIMARY KEY (`id`),"
    "UNIQUE KEY `key` (`key`)"
    ") ENGINE=InnoDB DEFAULT CHARSET=utf8;";
    db.update(sql);
  }

  ASSERT_EQ(GetMaxAddressId(db), 1);
  ASSERT_EQ(GetMaxAddressId(db), 2);
  ASSERT_EQ(GetMaxAddressId(db), 3);

  // 不drop table.0_explorer_meta 后面测试用例会用到
}


TEST(Util, GetAddressIds) {
  const string uri = Config::GConfig.get("testdb.uri", "");
  if (uri.empty()) {
    LOG_WARN("skipped, test: [Util][GetAddressIds]");
    return;
  }

  string sql;
  MySQLConnection db(uri);
  {
    // drop table
    sql = "DROP TABLE IF EXISTS `addresses_0015`;";
    db.update(sql);
    sql = "DROP TABLE IF EXISTS `addresses_0022`;";
    db.update(sql);
  }

  {
    // create table
    // 1Dhx3kGVkLaVFDYacZARheNzAWhYPTxHLq -> addresses_0015
    sql = "CREATE TABLE `addresses_0015` ("
    "`id` bigint(20) NOT NULL,"
    "`address` varchar(35) NOT NULL,"
    "`tx_count` int(11) NOT NULL DEFAULT '0',"
    "`total_received` bigint(20) NOT NULL DEFAULT '0',"
    "`total_sent` bigint(20) NOT NULL DEFAULT '0',"
    "`created_at` datetime NOT NULL,"
    "`updated_at` datetime NOT NULL,"
    "PRIMARY KEY (`id`),"
    "UNIQUE KEY `address` (`address`)"
    ") ENGINE=InnoDB DEFAULT CHARSET=utf8;";
    db.update(sql);

    // 1LrM4bojLAKfuoFMXkDtVPMGydX1rkaMqH -> addresses_0022
    sql = "CREATE TABLE `addresses_0022` ("
    "`id` bigint(20) NOT NULL,"
    "`address` varchar(35) NOT NULL,"
    "`tx_count` int(11) NOT NULL DEFAULT '0',"
    "`total_received` bigint(20) NOT NULL DEFAULT '0',"
    "`total_sent` bigint(20) NOT NULL DEFAULT '0',"
    "`created_at` datetime NOT NULL,"
    "`updated_at` datetime NOT NULL,"
    "PRIMARY KEY (`id`),"
    "UNIQUE KEY `address` (`address`)"
    ") ENGINE=InnoDB DEFAULT CHARSET=utf8;";
    db.update(sql);
  }

  {
    std::set<std::string> allAddresss;
    map<string, int64_t> addrMap;
    allAddresss.insert("1Dhx3kGVkLaVFDYacZARheNzAWhYPTxHLq");
    ASSERT_EQ(GetAddressIds(uri, allAddresss, addrMap), true);
    ASSERT_EQ(addrMap.size(), 1);
    const int64_t addrId1 = addrMap["1Dhx3kGVkLaVFDYacZARheNzAWhYPTxHLq"];

    allAddresss.insert("1LrM4bojLAKfuoFMXkDtVPMGydX1rkaMqH");
    addrMap.clear();
    ASSERT_EQ(GetAddressIds(uri, allAddresss, addrMap), true);
    ASSERT_EQ(addrMap.size(), 2);
    ASSERT_EQ(addrMap["1Dhx3kGVkLaVFDYacZARheNzAWhYPTxHLq"], addrId1);
    ASSERT_EQ(addrMap["1LrM4bojLAKfuoFMXkDtVPMGydX1rkaMqH"], addrId1 + 1);

  }

  {
    // drop table
    sql = "DROP TABLE IF EXISTS `addresses_0015`;";
    db.update(sql);
    sql = "DROP TABLE IF EXISTS `addresses_0022`;";
    db.update(sql);
  }
}
