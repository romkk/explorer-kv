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

#include "Common.h"
#include "MySQLConnection.h"

#include "bitcoin/base58.h"
#include "bitcoin/util.h"

int32_t HexToDecLast2Bytes(const string &hex);
int32_t AddressTableIndex(const string &address);

int64_t GetMaxAddressId(MySQLConnection &db);
bool GetAddressIds(const string &dbURI, const set<string> &allAddresss,
                   map<string, int64_t> addrMap);

#endif
