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
#include "../src/Util.h"

TEST(Util, splitStr) {
  std::string s = "a,b,,Bb,";
  auto res = split(s, ',');
  ASSERT_EQ(res.size(), 4);
  ASSERT_EQ(res[0], "a");
  ASSERT_EQ(res[1], "b");
  ASSERT_EQ(res[2], "");
  ASSERT_EQ(res[3], "Bb");
}

TEST(Util, BoundedBuffer1) {
  BoundedBuffer<string> boundedBuffer(3);
  boundedBuffer.pushFront("1");
  boundedBuffer.pushFront("2");
  boundedBuffer.pushFront("3");

  string s;
  boundedBuffer.popBack(&s);
  ASSERT_EQ(s, "1");
  boundedBuffer.popBack(&s);
  ASSERT_EQ(s, "2");
  boundedBuffer.popBack(&s);
  ASSERT_EQ(s, "3");
}

TEST(Util, BoundedBuffer2) {
  string s;
  BoundedBuffer<string> boundedBuffer(3);

  boundedBuffer.pushFront("1");
  boundedBuffer.pushFront("2");
  boundedBuffer.pushFront("3");
  boundedBuffer.popBack(&s);
  ASSERT_EQ(s, "1");
  boundedBuffer.pushFront("4");

  boundedBuffer.popBack(&s);
  ASSERT_EQ(s, "2");
  boundedBuffer.popBack(&s);
  ASSERT_EQ(s, "3");
  boundedBuffer.popBack(&s);
  ASSERT_EQ(s, "4");
}
