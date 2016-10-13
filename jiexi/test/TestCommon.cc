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

TEST(Common, score2Str) {
  // 10e-25
  ASSERT_EQ(score2Str(0.0000000000000000000000001), "0.0000000000000000000000001");
  ASSERT_EQ(score2Str(0.0000000000000000000000009), "0.0000000000000000000000009");
  ASSERT_EQ(score2Str(0.000000000000000000000001),  "0.0000000000000000000000010");
  ASSERT_EQ(score2Str(0.00000000000000000000001),   "0.0000000000000000000000100");
  ASSERT_EQ(score2Str(0.0000000000000000000001),    "0.0000000000000000000001000");
  ASSERT_EQ(score2Str(0.000000000000000000001), "0.0000000000000000000010000");
  ASSERT_EQ(score2Str(0.00000000000000000001),  "0.0000000000000000000100000");
  ASSERT_EQ(score2Str(0.0000000000000000001),   "0.0000000000000000001000000");
  ASSERT_EQ(score2Str(0.000000000000000001),    "0.0000000000000000010000000");
  ASSERT_EQ(score2Str(0.00000000000000001), "0.0000000000000000100000000");
  ASSERT_EQ(score2Str(0.0000000000000001),  "0.0000000000000001000000000");
  ASSERT_EQ(score2Str(0.000000000000001),   "0.0000000000000010000000000");
  ASSERT_EQ(score2Str(0.00000000000001),    "0.0000000000000100000000000");
  ASSERT_EQ(score2Str(0.0000000000001), "0.0000000000001000000000000");
  ASSERT_EQ(score2Str(0.000000000001),  "0.0000000000010000000000000");
  ASSERT_EQ(score2Str(0.00000000001),   "0.0000000000100000000000000");
  // 下面几个没有那么多有效位数了：1之后的零的个数减少
  ASSERT_EQ(score2Str(0.0000000001),    "0.000000000100000000000000");
  ASSERT_EQ(score2Str(0.000000001), "0.00000000100000000000000");
  ASSERT_EQ(score2Str(0.00000001),  "0.0000000100000000000000");
  ASSERT_EQ(score2Str(0.0000001),   "0.000000100000000000000");
  ASSERT_EQ(score2Str(0.000001),    "0.00000100000000000000");
  ASSERT_EQ(score2Str(0.00001), "0.0000100000000000000");
  ASSERT_EQ(score2Str(0.0001),  "0.000100000000000000");
  ASSERT_EQ(score2Str(0.001),   "0.00100000000000000");
  ASSERT_EQ(score2Str(0.01),    "0.0100000000000000");
  ASSERT_EQ(score2Str(0.1), "0.100000000000000");
  ASSERT_EQ(score2Str(1.0), "1.00000000000000");
  ASSERT_EQ(score2Str(10.0),    "10.0000000000000");
  ASSERT_EQ(score2Str(100.0),   "100.000000000000");
  ASSERT_EQ(score2Str(1000.0),  "1000.00000000000");
  ASSERT_EQ(score2Str(10000.0), "10000.0000000000");
  ASSERT_EQ(score2Str(100000.0),    "100000.000000000");
  ASSERT_EQ(score2Str(1000000.0),   "1000000.00000000");
  ASSERT_EQ(score2Str(10000000.0),  "10000000.0000000");
  ASSERT_EQ(score2Str(100000000.0), "100000000.000000");

  // 下面应该只有14个有效
  ASSERT_EQ(score2Str(123412345678.0), "123412345678.00");
  ASSERT_EQ(score2Str(1234.12345678123), "1234.1234567812");
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