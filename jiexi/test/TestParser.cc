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

#include <signal.h>
#include <execinfo.h>
#include <string>
#include "gtest/gtest.h"
#include "Common.h"
#include "Parser.h"


TEST(Parser, _insertOrphanBlockHash) {
  string value;
  _insertOrphanBlockHash("000000000000000000ab7b48efb0b1bdcb7dccce4172bea1a7d2f5d547ab710d", value);
  ASSERT_EQ(value.size(), 64);
  _insertOrphanBlockHash("00000000000000000ccc5070d11438cd2843bd4fc530878306cd102f979e9183", value);
  ASSERT_EQ(value.size(), 128);
  _insertOrphanBlockHash("0000000000000000091258d0cbc0f1b3ad258f947984a44c8d67aecd5c94bfee", value);
  ASSERT_EQ(value.size(), 192);

  _removeOrphanBlockHash("00000000000000000ccc5070d11438cd2843bd4fc530878306cd102f979e9183", value);
  ASSERT_EQ(value.size(), 128);
  _removeOrphanBlockHash("0000000000000000091258d0cbc0f1b3ad258f947984a44c8d67aecd5c94bfee", value);
  ASSERT_EQ(value.size(), 64);
  _removeOrphanBlockHash("000000000000000000ab7b48efb0b1bdcb7dccce4172bea1a7d2f5d547ab710d", value);
  ASSERT_EQ(value.size(), 0);
}

