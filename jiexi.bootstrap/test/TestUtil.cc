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

TEST(Util, split) {
  string s = "a,b,,Bb,";
  auto res = split(s, ',');
  ASSERT_EQ(res.size(), 4);
  ASSERT_EQ(res[0], "a");
  ASSERT_EQ(res[1], "b");
  ASSERT_EQ(res[2], "");
  ASSERT_EQ(res[3], "Bb");
}

TEST(Util, AddressTableIndex) {
  // 3QzX5xzpwRgnRaB4ZRWH5rc95gC65K4JmW: ff9a4ed72c912d58fd11568b2108bb6d34b563b6
  // 0xb6 = 182
  string address = "3QzX5xzpwRgnRaB4ZRWH5rc95gC65K4JmW";
  ASSERT_EQ(AddressTableIndex(address), 182);

  // 3BcPtPnLVY9TCWPhkQhoJCvHKxZjU1r3uU: 6cd18302962df6b7d05bb31425d2b1fc55a03d46
  // 0x46 = 70
  address = "3BcPtPnLVY9TCWPhkQhoJCvHKxZjU1r3uU";
  ASSERT_EQ(AddressTableIndex(address), 70);

  // 1EQs14LB5sNg8rbADeZhHfe2B9B8Crvya4: 931cf0e7a87a6bf34552fa23413dc43a3a730849
  // 0x49 = 73
  address = "1EQs14LB5sNg8rbADeZhHfe2B9B8Crvya4";
  ASSERT_EQ(AddressTableIndex(address), 73);

  // 1Pbm1gD6SXNWdCu5XwmhXkQYusxM4fyeqn: f7e5a62669d0e1a99993ccd4978a082565405af2
  // 0xf2 = 242
  address = "1Pbm1gD6SXNWdCu5XwmhXkQYusxM4fyeqn";
  ASSERT_EQ(AddressTableIndex(address), 242);
}
