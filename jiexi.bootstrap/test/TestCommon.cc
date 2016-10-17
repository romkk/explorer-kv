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

TEST(Common, Strings_Format) {
  for (int i = 1; i < 1024; i++) {
    string s;
    for (int j = 0; j < i; j++) {
      s += "A";
    }
    string s1 = Strings::Format("%s", s.c_str());
    ASSERT_EQ(s1, s);
  }
}

TEST(Common, Strings_Append) {
  for (int i = 1; i < 1024; i++) {
    string s;
    for (int j = 0; j < i; j++) {
      s += "A";
    }
    string s1;
    Strings::Append(s1, "%s", s.c_str());
    ASSERT_EQ(s1, s);
  }
}

TEST(Common, IP2StrAndStr2Ip) {
  ASSERT_EQ(Str2Ip("10.0.0.0"), 167772160u);
  ASSERT_EQ(Ip2Str(167772160u), "10.0.0.0");
}

TEST(Common, isLanIp) {
  // A: 10.0.0.0--10.255.255.255
  ASSERT_EQ(isLanIP(Str2Ip("10.0.0.0")), true);
  ASSERT_EQ(isLanIP(Str2Ip("10.255.255.255")), true);
  ASSERT_EQ(isLanIP(Str2Ip("11.0.0.0")), false);
  ASSERT_EQ(isLanIP(Str2Ip("9.255.255.255")), false);

  // B: 172.16.0.0--172.31.255.255
  ASSERT_EQ(isLanIP(Str2Ip("172.16.0.0")), true);
  ASSERT_EQ(isLanIP(Str2Ip("172.31.255.255")), true);
  ASSERT_EQ(isLanIP(Str2Ip("172.15.255.255")), false);
  ASSERT_EQ(isLanIP(Str2Ip("172.32.0.0")), false);

  // C: 192.168.0.0 - 192.168.255.255
  ASSERT_EQ(isLanIP(Str2Ip("192.168.0.0")), true);
  ASSERT_EQ(isLanIP(Str2Ip("192.168.255.255")), true);
  ASSERT_EQ(isLanIP(Str2Ip("192.169.0.0")), false);
  ASSERT_EQ(isLanIP(Str2Ip("192.167.255.255")), false);
}

TEST(Common, date) {
  // 1418051254 -> 2014-12-08 15:07:34 GMT
  ASSERT_EQ(date("%F %T", 1418051254), "2014-12-08 15:07:34");
  ASSERT_EQ(str2time("2014-12-08 15:07:34", "%F %T"), 1418051254);
}

TEST(Common, BitsToDifficulty) {
  // 0x1b0404cb: https://en.bitcoin.it/wiki/Difficulty
  double d;
  BitsToDifficulty(0x1b0404cbu, &d);  // diff = 16307.420939
  ASSERT_EQ((uint64_t)(d * 10000.0), 163074209ull);
}
