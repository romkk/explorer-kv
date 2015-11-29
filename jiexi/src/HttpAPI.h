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

#ifndef EXPLORER_HTTPAPI_H_
#define EXPLORER_HTTPAPI_H_

#include <evhtp.h>

#include "Common.h"
#include "KVDB.h"
#include "Util.h"

#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

#include "explorer_generated.h"


#define API_ERROR_SUCCESS                0
#define API_ERROR_EMPTY_PARAMS           100
#define API_ERROR_TOO_MANY_PARAMS        101
#define API_ERROR_EMPTY_METHOD           102
#define API_ERROR_METHOD_NOT_REGISTERED  103

#define API_ERROR_INVALID_PARAMS   201

class APIResponse {
public:
	vector<uint8_t> data_;
	vector<int32_t> length_, offset_;
};

class APIInOut {
public:
  int32_t verbose_;
  int32_t pageNo_;
  int32_t pageSize_;
  
  int32_t errorNo_;
  string  errorMsg_;
  
  string  data_;
  
  APIInOut(int32_t verbose, int32_t pageNo, int32_t pageSize);
};

class APIServer {
	KVDB *kvdb_;
  string  listenHost_;
  int32_t listenPort_;
  
  evbase_t *evbase_;
  evhtp_t  *htp_;

  int32_t nThreads_;
  
public:
	APIServer();
	~APIServer();
  
  void setKVDB(KVDB *kvdb);
	
  void init();
  void run();
  void stop();
};

class APIHandler {
  KVDB *kvdb_;
  
public:
  APIHandler(KVDB *kvdb);
  
  void address(APIInOut &resp, evhtp_request_t *req);
};

#endif
