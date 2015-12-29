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

#include <boost/filesystem.hpp>

#include "gtest/gtest.h"
#include "Common.h"

#include "KVDB.h"
#include "test_generated.h"

TEST(FlatBuffers, Foobar2) {
  vector<int> arr;

  flatbuffers::FlatBufferBuilder fbb;
  fbb.ForceDefaults(true);

  auto fb_arr = fbb.CreateVector(arr);
  fbe::FooBar2Builder fooBar2Builder(fbb);
  fooBar2Builder.add_int_arr(fb_arr);
  fbb.Finish(fooBar2Builder.Finish());

  {
    auto fooBar2 = flatbuffers::GetRoot<fbe::FooBar2>(fbb.GetBufferPointer());
    ASSERT_EQ(fooBar2->int_arr()->size(), 0);
  }
}

TEST(FlatBuffers, EncodeDecode1) {
  const int32_t id1  = 0;
  const string name1 = "b94eca121befa93adca991b1c363983b41970721e22cb041a7c1d9f05ce843d7";
  const int32_t id2  = 1024;
  const string name2 = "205bfa48089a606d7c744d797bdb99cc07dd2e96949b6e1616d080a2ac2b5442";
  ASSERT_EQ(name1.length(), name2.length());
  string buffer;

  flatbuffers::FlatBufferBuilder fbb;
  fbb.ForceDefaults(true);
  auto fb_name1 = fbb.CreateString(name1);

  fbe::FooBarBuilder fooBarBuilder(fbb);
  fooBarBuilder.add_id(id1);
  fooBarBuilder.add_name(fb_name1);
  fbb.Finish(fooBarBuilder.Finish());

  auto bufferpointer = reinterpret_cast<const char *>(fbb.GetBufferPointer());
  buffer.assign(bufferpointer, bufferpointer + fbb.GetSize());

  ASSERT_EQ(buffer.size(), fbb.GetSize());
  ASSERT_EQ(memcmp(fbb.GetBufferPointer(), buffer.data(), fbb.GetSize()), 0);

  {
    auto fooBar = flatbuffers::GetRoot<fbe::FooBar>((void *)buffer.data());
    ASSERT_NE(fooBar, nullptr);
    ASSERT_EQ(fooBar->id(), id1);
    ASSERT_EQ(fooBar->name()->str(), name1);
  }

  // 测试修改字符串
  {
    auto fooBar = flatbuffers::GetMutableRoot<fbe::FooBar>((void *)buffer.data());
    ASSERT_NE(fooBar, nullptr);
    ASSERT_EQ(fooBar->id(), id1);
    ASSERT_EQ(fooBar->name()->str(), name1);

    for (flatbuffers::uoffset_t i = 0; i < name2.length(); i++) {
      fooBar->mutable_name()->Mutate(i, name2[i]);
    }
    ASSERT_EQ(fooBar->name()->str(), name2);

    ASSERT_EQ(fooBar->mutate_id(id2), true);
    ASSERT_EQ(fooBar->id(), id2);
  }
}


TEST(FlatBuffers, EncodeDecode2) {
  flatbuffers::FlatBufferBuilder fbb;
  fbb.ForceDefaults(true);

  fbe::FooBarBuilder fooBarBuilder(fbb);
  fooBarBuilder.add_id(100);
  fbb.Finish(fooBarBuilder.Finish());

  {
    auto fooBar = flatbuffers::GetRoot<fbe::FooBar>((void *)fbb.GetBufferPointer());
    ASSERT_NE(fooBar, nullptr);
    ASSERT_EQ(fooBar->id(), 100);

    // 下面这行会导致程序崩溃，必须设置否则无法取出字符串
//    ASSERT_EQ(fooBar->name()->str().size(), 0);
  }
}
