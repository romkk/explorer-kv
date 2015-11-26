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

#include <stdio.h>
#include <evhtp.h>

#include "rocksdb/db.h"
#include "rocksdb/slice.h"
#include "rocksdb/options.h"

#include "Common.h"
#include "KVDB.h"
#include "Util.h"

#include "explorer_generated.h"

typedef void (*handleFunction)(evhtp_request_t *req, const vector<string> &params, const string &queryId);

#define API_ERROR_EMPTY_PARAMS           100
#define API_ERROR_TOO_MANY_PARAMS        101
#define API_ERROR_EMPTY_METHOD           102
#define API_ERROR_METHOD_NOT_REGISTERED  103

// global vars
KVDB *gDB = nullptr;
std::unordered_map<string, handleFunction> gHandleFunctions;

void dbGetKeys(const vector<string> &keys,
               vector<int32_t> &length, vector<int32_t> &offset, vector<uint8_t> &data) {
  length.resize(keys.size(),  0);
  offset.resize(keys.size(), -1);

  vector<string> buffer(keys.size());
  gDB->multiGet(keys, buffer);
  assert(buffer.size() == keys.size());

  for (size_t i = 0; i < keys.size(); i++) {
    if (buffer[i].size() == 0) {
      continue;
    }
    offset[i] = (int32_t)data.size();
    length[i] = (int32_t)buffer[i].size();
    data.reserve(data.size() + buffer[i].size());
    data.insert(data.end(), buffer[i].begin(), buffer[i].end());
  }
}

void cb_error(evhtp_request_t * req, const int32_t error_no, const char *error_msg) {
  flatbuffers::FlatBufferBuilder fbb;
  auto fb_errorMsg = fbb.CreateString(error_msg);

  fbe::APIResponseBuilder apiResponseBuilder(fbb);
  apiResponseBuilder.add_error_no(error_no);
  apiResponseBuilder.add_error_msg(fb_errorMsg);
  apiResponseBuilder.Finish();

  evbuffer_add_reference(req->buffer_out, fbb.GetBufferPointer(), fbb.GetSize(), NULL, NULL);
  evhtp_send_reply(req, EVHTP_RES_400);
}

void cb_root(evhtp_request_t * req, void *ptr) {
  string s = Strings::Format("btc.com Explorer API server, %s (UTC+0)", date("%F %T").c_str());
  evbuffer_add_reference(req->buffer_out, s.c_str(), s.length(), NULL, NULL);
  evhtp_send_reply(req, EVHTP_RES_NOTFOUND);
}

void output(evhtp_request_t *req, const string &queryId,
            const vector<uint8_t> &data, const vector<int32_t> &length, const vector<int32_t> &offset) {
  flatbuffers::FlatBufferBuilder fbb;
  auto fb_lengthArr = fbb.CreateVector(length);
  auto fb_offsetArr = fbb.CreateVector(offset);
  auto fb_data      = fbb.CreateVector(data);
  auto fb_queryId   = fbb.CreateString(queryId);
  fbe::APIResponseBuilder apiResp(fbb);
  apiResp.add_id(fb_queryId);
  apiResp.add_length_arr(fb_lengthArr);
  apiResp.add_offset_arr(fb_offsetArr);
  apiResp.add_data(fb_data);
  apiResp.Finish();

  evbuffer_add_reference(req->buffer_out, fbb.GetBufferPointer(), fbb.GetSize(), NULL, NULL);
}

void handle_get(evhtp_request_t *req, const vector<string> &params, const string &queryId) {
  if (params.size() == 0) {
    cb_error(req, API_ERROR_EMPTY_PARAMS, "params is empty");
    return;
  }

  vector<uint8_t> data;
  vector<int32_t> length, offset;
  // params 即 keys
  dbGetKeys(params, length, offset, data);

  output(req, queryId, data, length, offset);
  evhtp_send_reply(req, EVHTP_RES_OK);
}

void handle_ping(evhtp_request_t *req, const vector<string> &params, const string &queryId) {
  vector<uint8_t> data;
  vector<int32_t> length, offset;
  const string s = "pong";

  data.insert(data.end(), s.begin(), s.end());
  length.push_back((int32_t)s.size());
  offset.push_back(0);

  output(req, queryId, data, length, offset);
  evhtp_send_reply(req, EVHTP_RES_OK);
}

void cb_kv(evhtp_request_t *req, void *ptr) {
  static const int32_t kMaxParamsCount = 1000;

  const char *queryMethod = evhtp_kv_find(req->uri->query, "method");
  const char *queryParams = evhtp_kv_find(req->uri->query, "params");
  string queryId;
  if (evhtp_kv_find(req->uri->query, "id") == nullptr) {
    queryId = Strings::Format("rocksdb-%lld", Time::CurrentTimeMill());
  } else {
    queryId = string(evhtp_kv_find(req->uri->query, "id"));
  }

  vector<string> params;
  if (queryParams != nullptr) {
    params = split(string(queryParams), ',', kMaxParamsCount + 1);
    for (auto &param : params) {
      param = UrlDecode(param.c_str());
    }
  }
  if (params.size() > kMaxParamsCount) {
    cb_error(req, API_ERROR_TOO_MANY_PARAMS,
             Strings::Format("too many params, max: %d", kMaxParamsCount).c_str());
    return;
  }
  if (queryMethod == nullptr) {
    cb_error(req, API_ERROR_EMPTY_METHOD, "method is empty");
    return;
  }

  const auto it = gHandleFunctions.find(string(queryMethod));
  if (it != gHandleFunctions.end()) {
    (*it->second)(req, params, queryId);;
    return;
  }

  // method not found
  cb_error(req, API_ERROR_METHOD_NOT_REGISTERED, "method is not registered");
}


int main(int argc, char ** argv) {
  // 注册方法名称
  gHandleFunctions["get"]  = handle_get;
  gHandleFunctions["ping"] = handle_ping;

  gDB = new KVDB("./rocksdb");
  gDB->open();
  {
    // for test
    gDB->set("test01", Strings::Format("value%lld", Time::CurrentTimeMill()));
    gDB->set("test02", Strings::Format("value%lld", Time::CurrentTimeMill()));
    gDB->set("test03", Strings::Format("value%lld", Time::CurrentTimeMill()));
  }

  evbase_t * evbase = event_base_new();
  evhtp_t  * htp    = evhtp_new(evbase, NULL);

  evhtp_set_cb(htp, "/",   cb_root, NULL);
  evhtp_set_cb(htp, "/kv", cb_kv,  NULL);

  evhtp_bind_socket(htp, "0.0.0.0", 8081, 1024);

  event_base_loop(evbase, 0);  // while(1)

  // release resources
  evhtp_unbind_socket(htp);
  evhtp_free(htp);
  event_base_free(evbase);
  delete gDB;

  return 0;
}
