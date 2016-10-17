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

#ifndef BLOCK_IMPORTER_H_
#define BLOCK_IMPORTER_H_

#include "Common.h"
#include "Util.h"

class BlockImporter {
  mutex lock_;
  atomic<bool> running_;

  int32_t currHeight_;
  int32_t beginHeight_;
  int32_t endHeight_;

  vector<string *> dataVec_;

  int32_t nProduceThreads_;
  atomic<int32_t> runningProduceThreads_;
  atomic<int32_t> runningConsumeThreads_;

  string dir_;
  FILE *f_;
  string bitcoindUri_;

  void writeDisk(const string *data, int32_t height);
  void threadProduceBlock();
  void threadConsumeBlock();

public:
  BlockImporter(const string &dir, const int32_t nProduceThreads,
                const string &bitcoindUri,
                int32_t beginHeight, int32_t endHeight);
  ~BlockImporter();

  void run();
  void stop();
};

#endif
