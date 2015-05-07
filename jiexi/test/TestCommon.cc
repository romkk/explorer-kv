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

TEST(Common, bitsdiff) {
  ASSERT_EQ(DiffToBits(1), 0x1d00ffff);
  ASSERT_EQ(DiffToBits(2), 0x1c7fff80);
  ASSERT_EQ(DiffToBits(4), 0x1c3fffc0);
  ASSERT_EQ(DiffToBits(6), 0x1c2aaa80);
  ASSERT_EQ(DiffToBits(384), 0x1c00aaaa);
  ASSERT_EQ(DiffToBits(384*256), 0x1b00aaaa);
}

TEST(Common, TargetToBdiff) {
  // 0x00000000FFFF0000000000000000000000000000000000000000000000000000 /
  // 0x00000000000404CB000000000000000000000000000000000000000000000000
  // = 16307.420938523983 (bdiff)
  ASSERT_EQ(TargetToBdiff("0x00000000000404CB000000000000000000000000000000000000000000000000"), 16307);
}


TEST(Common, TargetToPdiff) {
  // 0x00000000FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF /
  // 0x00000000000404CB000000000000000000000000000000000000000000000000
  // = 16307.669773817162 (pdiff)
  ASSERT_EQ(TargetToBdiff("0x00000000000404CB000000000000000000000000000000000000000000000000"), 16307);
}

TEST(Common, VerifyMessage) {
  bool res = VerifyMessage("1HZwkjkeaoZfTSaJxDw6aKkxp45agDiEzN",
                           "HKxvict8nte+YLbXaYdma4bLytJ7gaeSwCY8zayzEmSqR3F/rB/XaaFS/U4SUAL2XSubsPLqLFqllEndu6nDGr8=",
                           "This is an example of a signed message.");
  ASSERT_EQ(res, true);
  
  res = VerifyMessage("1HZwkjkeaoZfTSaJxDw6aKkxp45agDiEz1",
                      "HKxvict8nte+YLbXaYdma4bLytJ7gaeSwCY8zayzEmSqR3F/rB/XaaFS/U4SUAL2XSubsPLqLFqllEndu6nDGr8=",
                      "This is an example of a signed message.");
  ASSERT_EQ(res, false);
  
  res = VerifyMessage("1HZwkjkeaoZfTSaJxDw6aKkxp45agDiEzN",
                      "JKxvict8nte+YLbXaYdma4bLytJ7gaeSwCY8zayzEmSqR3F/rB/XaaFS/U4SUAL2XSubsPLqLFqllEndu6nDGr8=",
                      "This is an example of a signed message.");
  ASSERT_EQ(res, false);
  
  res = VerifyMessage("1HZwkjkeaoZfTSaJxDw6aKkxp45agDiEzN",
                      "HKxvict8nte+YLbXaYdma4bLytJ7gaeSwCY8zayzEmSqR3F/rB/XaaFS/U4SUAL2XSubsPLqLFqllEndu6nDGr8=",
                      "this is an example of a signed message.");
  ASSERT_EQ(res, false);
}

TEST(Common, SignMessage) {
  // address:
  //   1MgwqdXUhxL6Ba96TbDGpjnQeoyYZ2KsLm (Compressed)
  //   1NEojqoUPRutfCo5rWB5xdfZs51uocFqr  (unCompressed)
  // private:
  //   b58: L5Cts73AZr2SPTHgpuumC3maBeMt2zw14PbL4mHgsYsoaHcPrP99
  //   hex: EE478AEAB75CBDA07B53AA797C046286AB3A83760C075AE93477C3A9C3CB6874
  const unsigned char privKey[] = {
    0xEE, 0x47, 0x8A, 0xEA, 0xB7, 0x5C, 0xBD, 0xA0, 0x7B, 0x53, 0xAA, 0x79, 0x7C,
    0x04, 0x62, 0x86, 0xAB, 0x3A, 0x83, 0x76, 0x0C, 0x07, 0x5A, 0xE9, 0x34, 0x77,
    0xC3, 0xA9, 0xC3, 0xCB, 0x68, 0x74};
  bool verifyRes;
  CKey key;
  string signature, strMessage = "this sign message";
  
  // test Compressed
  key.Set(privKey, privKey + sizeof(privKey)/sizeof(privKey[0]), true);
  ASSERT_EQ(SignMessage(key, strMessage, signature), true);
  verifyRes = VerifyMessage("1MgwqdXUhxL6Ba96TbDGpjnQeoyYZ2KsLm", signature, strMessage);
  ASSERT_EQ(verifyRes, true);
  
  // test unCompressed
  key.Set(privKey, privKey + sizeof(privKey)/sizeof(privKey[0]), false);
  ASSERT_EQ(SignMessage(key, strMessage, signature), true);
  verifyRes = VerifyMessage("1NEojqoUPRutfCo5rWB5xdfZs51uocFqr", signature, strMessage);
  ASSERT_EQ(verifyRes, true);

  // test Compressed
  key.Set(privKey, privKey + sizeof(privKey)/sizeof(privKey[0]), false);
  ASSERT_EQ(SignMessage(key, strMessage, signature), true);
  verifyRes = VerifyMessage("1MgwqdXUhxL6Ba96TbDGpjnQeoyYZ2KsLm", signature, strMessage);
  ASSERT_EQ(verifyRes, false);
  
  // test unCompressed
  key.Set(privKey, privKey + sizeof(privKey)/sizeof(privKey[0]), true);
  ASSERT_EQ(SignMessage(key, strMessage, signature), true);
  verifyRes = VerifyMessage("1NEojqoUPRutfCo5rWB5xdfZs51uocFqr", signature, strMessage);
  ASSERT_EQ(verifyRes, false);
}
