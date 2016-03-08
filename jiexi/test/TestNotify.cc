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
#include "NotifyLog.h"


TEST(Notify, NotifyItem) {
  NotifyItem item;
  vector<string> lines;
  lines.push_back("2016-01-01 12:13:14,10,401656,000000000000000004dd73b0dc84ae2a7bb790d92a3fb457cafca2c221206aa0");
  lines.push_back("2016-01-01 12:13:14,11,401626,000000000000000002614acdf9e68a2d5b1d5782cf54bceecba33d1760313b40");
  lines.push_back("2016-01-01 12:13:14,20,18KsvEx5zhNtsatBsTqDvKV5wk73GwSdXx,-1,f3f13a843b8f69a850ad8e45becae5ccd9cc3f5c85f5eb78a54bb9971c50ed9b,725865");
  lines.push_back("2016-01-01 12:13:14,21,18KsvEx5zhNtsatBsTqDvKV5wk73GwSdXx,401626,f3f13a843b8f69a850ad8e45becae5ccd9cc3f5c85f5eb78a54bb9971c50ed9b,725865");
  lines.push_back("2016-01-01 12:13:14,22,18KsvEx5zhNtsatBsTqDvKV5wk73GwSdXx,401626,f3f13a843b8f69a850ad8e45becae5ccd9cc3f5c85f5eb78a54bb9971c50ed9b,725865");
  lines.push_back("2016-01-01 12:13:14,23,18KsvEx5zhNtsatBsTqDvKV5wk73GwSdXx,401626,f3f13a843b8f69a850ad8e45becae5ccd9cc3f5c85f5eb78a54bb9971c50ed9b,725865");

  for (const auto &l : lines) {
    item.parse(l);
    ASSERT_EQ(item.toStr(), l);
  }

  item.reset();
  ASSERT_EQ(item.toStr(), "");
}

TEST(Notify, NotifyItem2) {
  NotifyItem item;

  // load tx
  item.loadtx(20, "18KsvEx5zhNtsatBsTqDvKV5wk73GwSdXx",
              uint256("f3f13a843b8f69a850ad8e45becae5ccd9cc3f5c85f5eb78a54bb9971c50ed9b"),
              -1, 725865);
  item.timestamp_ = str2time("2016-01-01 12:13:14");
  ASSERT_EQ(item.toStr(), "2016-01-01 12:13:14,20,18KsvEx5zhNtsatBsTqDvKV5wk73GwSdXx,-1,f3f13a843b8f69a850ad8e45becae5ccd9cc3f5c85f5eb78a54bb9971c50ed9b,725865");

  // load block
  item.loadblock(10, uint256("000000000000000004dd73b0dc84ae2a7bb790d92a3fb457cafca2c221206aa0"), 401656);
  item.timestamp_ = str2time("2016-01-01 12:13:14");
  ASSERT_EQ(item.toStr(), "2016-01-01 12:13:14,10,401656,000000000000000004dd73b0dc84ae2a7bb790d92a3fb457cafca2c221206aa0");
}

