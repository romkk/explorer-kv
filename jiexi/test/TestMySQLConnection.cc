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
#include "MySQLConnection.h"

TEST(MySQLConnection, query) {
  string uri = Config::GConfig.get("testdb.uri", "");
  if (uri.empty()) {
    LOG_WARN("skipped");
    return;
  }

  MySQLConnection conn(uri);
  conn.execute("DROP TABLE IF EXISTS test");
  conn.execute("CREATE TABLE test(id INT, label CHAR(2))");
  uint64 ret = conn.update("INSERT INTO test(id, label) VALUES (1, 'a')");
  ASSERT_EQ(1, ret);
  MySQLResult result;
  conn.query("SELECT * from test", result);
  ASSERT_EQ(1, result.numRows());
  ASSERT_EQ(2, result.fields());
}
