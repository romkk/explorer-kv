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
#include "Common.h"
#include "URI.h"

bool testParse(const string & uri) {
  URI t = URI();
  bool ret = t.parse(uri);
  if (ret && (t.getUri() == uri)) {
    LOG("OK: %s", uri.c_str());
    //LOG("RET: %s", t.getDescription().c_str());
  } else {
    LOG("FAILED: %s", uri.c_str());
  }
  return ret;
}

TEST(URI, Parse) {
  ASSERT_TRUE(testParse("http://root:rootpass@master-00.vmware.com:8020/path?query#fragment"));
  ASSERT_TRUE(testParse("https://master-00.apache.org:8020/path?a=1&b=2"));
  ASSERT_TRUE(testParse("bitcoind://localhost:8333"));
  ASSERT_TRUE(testParse("mysql://root@10.111.90.10/testdb"));
  ASSERT_TRUE(testParse("file:///home/hadoop"));
  ASSERT_TRUE(testParse("/home/hadoop"));
  ASSERT_TRUE(testParse("http://www.google.com/path?query#fragment%20"));
}

TEST(URI, ParsePath) {
  URI t = URI();
  ASSERT_TRUE(t.parse("10.1.1.1"));
  ASSERT_EQ("10.1.1.1", t.getHost());
  ASSERT_TRUE(t.parse("/home/hadoop"));
  ASSERT_EQ("/home/hadoop", t.getPath());
  ASSERT_TRUE(t.parse("../../hadoop"));
  ASSERT_EQ("../../hadoop", t.getPath());
  ASSERT_TRUE(t.parse("hadoop"));
  ASSERT_EQ("hadoop", t.getHost());
}

TEST(URI, Get) {
  URI t;
  t.parse("mysql://root:rootpass@master-00.apache.org:8020/path?query#fragment");
  ASSERT_EQ(t.getScheme(), "mysql");
  ASSERT_EQ(t.getUser(), "root");
  ASSERT_EQ(t.getPassword(), "rootpass");
  ASSERT_EQ(t.getHost(), "master-00.apache.org");
  ASSERT_EQ(t.getPort(), 8020);
  ASSERT_EQ(t.getPath(), "/path");
  ASSERT_EQ(t.getQuery(), "query");
  ASSERT_EQ(t.getFragment(), "fragment");
}

TEST(URI, Set) {
  URI t;
  t.parse("mysql://root:rootpass@master-00.apache.org:8020");
  t.setUserInfo("newuser", "newpass");
  ASSERT_EQ(t.getUri(), "mysql://newuser:newpass@master-00.apache.org:8020");
  t.setPath("/test");
  ASSERT_EQ(t.getUri(), "mysql://newuser:newpass@master-00.apache.org:8020/test");
  t.setScheme("mysql");
  ASSERT_EQ(t.getUri(), "mysql://newuser:newpass@master-00.apache.org:8020/test");
}
