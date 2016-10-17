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

#ifndef Explorer_Util_h
#define Explorer_Util_h

#include <string>
#include <sstream>
#include <vector>

#include "Common.h"

#include "bitcoin/base58.h"
#include "bitcoin/util.h"

#define BILLION 1000000000  // 10^9

vector<string> split(const string &s, const char delim);
vector<string> split(const string &s, const char delim, const int32_t limit);

size_t getNumberOfLines(const string &file);

//
// HTTP Get/Post
//
bool httpGET (const char *url, string &response, long timeoutMs);
bool httpGET (const char *url, const char *userpwd,
              string &response, long timeoutMs);
bool httpPOST(const char *url, const char *userpwd,
              const char *postData, string &response, long timeoutMs);
bool bitcoindRpcCall(const char *url, const char *userpwd, const char *reqData,
                     string &response);

#endif
