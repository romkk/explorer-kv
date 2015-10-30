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
#include "PreAddress.h"


TEST(PreAddress, PreAddress1) {
  PreAddressHolder addrHolder;

  ASSERT_EQ(addrHolder.isExist("1QCgcbkg5kpq84DPatb5bGDXcYWU7vzDba"), false);
  addrHolder.insert("1QCgcbkg5kpq84DPatb5bGDXcYWU7vzDba");
  ASSERT_EQ(addrHolder.isExist("1QCgcbkg5kpq84DPatb5bGDXcYWU7vzDba"), true);
  ASSERT_EQ(addrHolder.isExist("16hH5Du2kxo7oXBETv6wXYv5aFDuusMqBQ"), false);

  addrHolder.adjust();

  ASSERT_EQ(addrHolder.isExist("1QCgcbkg5kpq84DPatb5bGDXcYWU7vzDba"), true);
  ASSERT_EQ(addrHolder.isExist("16hH5Du2kxo7oXBETv6wXYv5aFDuusMqBQ"), false);
}


TEST(PreAddress, PreAddress2) {
  PreAddressHolder addrHolder;
  addrHolder.insert("1QCgcbkg5kpq84DPatb5bGDXcYWU7vzDba");
  addrHolder.insert("16hH5Du2kxo7oXBETv6wXYv5aFDuusMqBQ");

  ASSERT_EQ(addrHolder.isExist("1QCgcbkg5kpq84DPatb5bGDXcYWU7vzDba"), true);
  ASSERT_EQ(addrHolder.isExist("16hH5Du2kxo7oXBETv6wXYv5aFDuusMqBQ"), true);
  ASSERT_EQ(addrHolder.isExist("1MVwmZdMPu7vLgwPPjm6DxtrwrRtrHmurk"), false);

  addrHolder.adjust();

  addrHolder.insert("1LoHoUWjgr1Ks6RDG6xtAvT83D44SRjLn4");
  ASSERT_EQ(addrHolder.isExist("1LoHoUWjgr1Ks6RDG6xtAvT83D44SRjLn4"), true);

  ASSERT_EQ(addrHolder.isExist("1QCgcbkg5kpq84DPatb5bGDXcYWU7vzDba"), true);
  ASSERT_EQ(addrHolder.isExist("16hH5Du2kxo7oXBETv6wXYv5aFDuusMqBQ"), true);
  ASSERT_EQ(addrHolder.isExist("1MVwmZdMPu7vLgwPPjm6DxtrwrRtrHmurk"), false);
}
