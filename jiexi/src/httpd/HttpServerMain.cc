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

#define API_ERROR_SUCCESS                0
#define API_ERROR_EMPTY_PARAMS           100
#define API_ERROR_TOO_MANY_PARAMS        101
#define API_ERROR_EMPTY_METHOD           102
#define API_ERROR_METHOD_NOT_REGISTERED  103

// global vars
KVDB *gDB = nullptr;
std::unordered_map<string, handleFunction> gHandleFunctions;
std::vector<const char *> gKeyTypes;  // 键对应的类型

void output(evhtp_request_t *req, const string &queryId,
            const vector<uint8_t> &data, const vector<int32_t> &length,
            const vector<int32_t> &offset, const vector<string> &types,
            const int32_t error_no = API_ERROR_SUCCESS, const char *error_msg = nullptr);


////////////////////////////////////////////////////////////////////////////////


void dbGetKeys(const vector<string> &keys,
               vector<int32_t> &length, vector<int32_t> &offset,
               vector<string> &types, vector<uint8_t> &data) {
  assert(keys.size() != 0);
  length.resize(keys.size(),  0);
  offset.resize(keys.size(), -1);
  types.resize(keys.size(), "unknown");

  vector<string> buffer(keys.size());
  gDB->multiGet(keys, buffer);
  assert(buffer.size() == keys.size());

  for (size_t i = 0; i < keys.size(); i++) {
    // 设置type
    const int32_t keyPrefixInt = atoi(keys[i].substr(0, 2).c_str());
    if (keyPrefixInt >= 0 && keyPrefixInt < gKeyTypes.size() && gKeyTypes[keyPrefixInt] != nullptr) {
      types[i] = string(gKeyTypes[keyPrefixInt]);
    }

    // 设置data
    if (buffer[i].size() == 0) {
      continue;
    }
    offset[i] = (int32_t)data.size();
    length[i] = (int32_t)buffer[i].size();
    data.reserve(data.size() + buffer[i].size());
    data.insert(data.end(), buffer[i].begin(), buffer[i].end());
  }
}

void output_error(evhtp_request_t * req, const int32_t error_no, const char *error_msg) {
  vector<uint8_t> data;
  vector<int32_t> length, offset;
  vector<string> types;
  output(req, Strings::Format("rocksdb-%lld", Time::CurrentTimeMill()),
         data, length, offset, types, error_no, error_msg);
  evhtp_send_reply(req, EVHTP_RES_400);
}

void cb_root(evhtp_request_t * req, void *ptr) {
  string s = Strings::Format("btc.com Explorer API server, %s (UTC+0)", date("%F %T").c_str());
  evbuffer_add_reference(req->buffer_out, s.c_str(), s.length(), NULL, NULL);
  evhtp_send_reply(req, EVHTP_RES_OK);
}

void output(evhtp_request_t *req, const string &queryId,
            const vector<uint8_t> &data, const vector<int32_t> &length,
            const vector<int32_t> &offset, const vector<string> &types,
            const int32_t error_no, const char *error_msg) {
  flatbuffers::FlatBufferBuilder fbb;
  fbb.ForceDefaults(true);

  vector<flatbuffers::Offset<flatbuffers::String> > fb_typesVec;
  for (const auto &type : types) {
    fb_typesVec.push_back(fbb.CreateString(type));
  }
  auto fb_lengthArr = fbb.CreateVector(length);
  auto fb_offsetArr = fbb.CreateVector(offset);
  auto fb_types     = fbb.CreateVector(fb_typesVec);
  auto fb_data      = fbb.CreateVector(data);
  auto fb_queryId   = fbb.CreateString(queryId);
  auto fb_errorMsg  = fbb.CreateString(error_msg != nullptr ? error_msg : "");

  fbe::APIResponseBuilder apiResp(fbb);
  apiResp.add_error_no(error_no);
  apiResp.add_error_msg(fb_errorMsg);
  apiResp.add_id(fb_queryId);
  apiResp.add_length_arr(fb_lengthArr);
  apiResp.add_offset_arr(fb_offsetArr);
  apiResp.add_type_arr(fb_types);
  apiResp.add_data(fb_data);
  fbb.Finish(apiResp.Finish());

  evbuffer_add_reference(req->buffer_out, fbb.GetBufferPointer(), fbb.GetSize(), NULL, NULL);
}

void handle_get(evhtp_request_t *req, const vector<string> &params, const string &queryId) {
  if (params.size() == 0) {
    output_error(req, API_ERROR_EMPTY_PARAMS, "params is empty");
    return;
  }

  vector<uint8_t> data;
  vector<int32_t> length, offset;
  vector<string> types;
  // params 即 keys
  dbGetKeys(params, length, offset, types, data);

  output(req, queryId, data, length, offset, types);
  evhtp_send_reply(req, EVHTP_RES_OK);
}

void handle_ping(evhtp_request_t *req, const vector<string> &params, const string &queryId) {
  vector<uint8_t> data;
  vector<int32_t> length, offset;
  vector<string> types;
  const string s = "pong";

  data.insert(data.end(), s.begin(), s.end());
  length.push_back((int32_t)s.size());
  offset.push_back(0);
  types.push_back("string");

  output(req, queryId, data, length, offset, types);
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
    output_error(req, API_ERROR_TOO_MANY_PARAMS,
             Strings::Format("too many params, max: %d", kMaxParamsCount).c_str());
    return;
  }
  if (queryMethod == nullptr) {
    output_error(req, API_ERROR_EMPTY_METHOD, "method is empty");
    return;
  }

  const auto it = gHandleFunctions.find(string(queryMethod));
  if (it != gHandleFunctions.end()) {
    (*it->second)(req, params, queryId);;
    return;
  }

  // method not found
  output_error(req, API_ERROR_METHOD_NOT_REGISTERED, "method is not registered");
}


int main(int argc, char ** argv) {
  // 注册方法名称
  gHandleFunctions["get"]  = handle_get;
  gHandleFunctions["ping"] = handle_ping;

  // 注册键值对应名称
  gKeyTypes.resize(100, nullptr);
  gKeyTypes[01] = "Tx";
  gKeyTypes[02] = "TxSpentBy";
  gKeyTypes[10] = "string";
  gKeyTypes[11] = "Block";
  gKeyTypes[12] = "string";
  gKeyTypes[20] = "Address";
  gKeyTypes[21] = "AddressTx";
  gKeyTypes[22] = "string";
  gKeyTypes[23] = "AddressUnspent";
  gKeyTypes[24] = "AddressUnspentIdx";
  gKeyTypes[30] = "DoubleSpending";
  gKeyTypes[90] = "string";

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

//  evhtp_use_threads(htp, NULL, 8, NULL);
  evhtp_bind_socket(htp, "0.0.0.0", 8081, 1024);

  event_base_loop(evbase, 0);  // while(1)

  // release resources
  evhtp_unbind_socket(htp);
  evhtp_free(htp);
  event_base_free(evbase);
  delete gDB;

  return 0;
}
