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

#include "HttpAPI.h"


typedef void (*handleFunction)(evhtp_request_t *req, const vector<string> &params, const string &queryId);

// global vars
KVDB *gDB = nullptr;
std::unordered_map<string, handleFunction> gHandleFunctions;  // kv handle functions


//////////////////////////////// static functions //////////////////////////////
void cb_root(evhtp_request_t * req, void *ptr);
void cb_kv(evhtp_request_t *req, void *ptr);

void kv_handle_ping(evhtp_request_t *req, const vector<string> &params, const string &queryId);
void kv_handle_get(evhtp_request_t *req, const vector<string> &params, const string &queryId);

void dbGetKeys(const vector<string> &keys, APIResponse &resp);
void kv_output(evhtp_request_t *req, const string &queryId, const APIResponse &resp,
               const int32_t error_no = API_ERROR_SUCCESS, const char *error_msg = nullptr);


///////////////////////////////// data functions ///////////////////////////////

void dbGetKeys(const vector<string> &keys, APIResponse &resp) {
  assert(keys.size() != 0);
  resp.length_.resize(keys.size(),  0);
  resp.offset_.resize(keys.size(), -1);
  
  vector<string> buffer(keys.size());
  gDB->multiGet(keys, buffer);
  assert(buffer.size() == keys.size());
  
  for (size_t i = 0; i < keys.size(); i++) {
    // 设置data
    if (buffer[i].size() == 0) {
      continue;
    }
    resp.offset_[i] = (int32_t)resp.data_.size();
    resp.length_[i] = (int32_t)buffer[i].size();
    resp.data_.reserve(resp.data_.size() + buffer[i].size());
    resp.data_.insert(resp.data_.end(), buffer[i].begin(), buffer[i].end());
  }
}


////////////////////////////////// kv output ///////////////////////////////////

void kv_output_error(evhtp_request_t * req, const int32_t error_no, const char *error_msg) {
  APIResponse resp;
  const string queryId = Strings::Format("rocksdb-%lld", Time::CurrentTimeMill());
  kv_output(req, queryId, resp, error_no, error_msg);
  evhtp_send_reply(req, EVHTP_RES_400);
}

void kv_output(evhtp_request_t *req, const string &queryId,
               const APIResponse &resp,
               const int32_t error_no, const char *error_msg) {
  flatbuffers::FlatBufferBuilder fbb;
  fbb.ForceDefaults(true);
  
  auto fb_lengthArr = fbb.CreateVector(resp.length_);
  auto fb_offsetArr = fbb.CreateVector(resp.offset_);
  auto fb_data      = fbb.CreateVector(resp.data_);
  auto fb_queryId   = fbb.CreateString(queryId);
  auto fb_errorMsg  = fbb.CreateString(error_msg != nullptr ? error_msg : "");
  
  fbe::APIResponseBuilder apiResp(fbb);
  apiResp.add_error_no(error_no);
  apiResp.add_error_msg(fb_errorMsg);
  apiResp.add_id(fb_queryId);
  apiResp.add_length_arr(fb_lengthArr);
  apiResp.add_offset_arr(fb_offsetArr);
  apiResp.add_data(fb_data);
  fbb.Finish(apiResp.Finish());
  
  evbuffer_add_reference(req->buffer_out, fbb.GetBufferPointer(), fbb.GetSize(), NULL, NULL);
}


////////////////////////////////// kv handle functions //////////////////////////

void kv_handle_get(evhtp_request_t *req, const vector<string> &params, const string &queryId) {
  if (params.size() == 0) {
    kv_output_error(req, API_ERROR_EMPTY_PARAMS, "params is empty");
    return;
  }
  APIResponse resp;
  dbGetKeys(params, resp);  // params 即 keys
  kv_output(req, queryId, resp);
  evhtp_send_reply(req, EVHTP_RES_OK);
}

void kv_handle_ping(evhtp_request_t *req, const vector<string> &params, const string &queryId) {
  const string s = "pong";
  APIResponse resp;
  resp.data_.insert(resp.data_.end(), s.begin(), s.end());
  resp.length_.push_back((int32_t)s.size());
  resp.offset_.push_back(0);
  kv_output(req, queryId, resp);
  evhtp_send_reply(req, EVHTP_RES_OK);
}


////////////////////////////// evhtp callback function //////////////////////////

void cb_root(evhtp_request_t * req, void *ptr) {
  string s = Strings::Format("btc.com Explorer API server, %s (UTC+0)", date("%F %T").c_str());
  evbuffer_add_reference(req->buffer_out, s.c_str(), s.length(), NULL, NULL);
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
    kv_output_error(req, API_ERROR_TOO_MANY_PARAMS,
                    Strings::Format("too many params, max: %d", kMaxParamsCount).c_str());
    return;
  }
  if (queryMethod == nullptr) {
    kv_output_error(req, API_ERROR_EMPTY_METHOD, "method is empty");
    return;
  }
  
  const auto it = gHandleFunctions.find(string(queryMethod));
  if (it != gHandleFunctions.end()) {
    (*it->second)(req, params, queryId);;
    return;
  }
  
  // method not found
  kv_output_error(req, API_ERROR_METHOD_NOT_REGISTERED, "method is not registered");
}


/////////////////////////////////// APIServer //////////////////////////////////

APIServer::APIServer() {
  listenHost_ = Config::GConfig.get("apiserver.listen.host", "0.0.0.0");
  listenPort_ = (int32_t)Config::GConfig.getInt("apiserver.listen.port", 8080);
  nThreads_   = (int32_t)Config::GConfig.getInt("apiserver.nthreads", 1);
}

APIServer::~APIServer() {
  stop();
}

void APIServer::setKVDB(KVDB *kvdb) {
  gDB = kvdb_ = kvdb;
}

void APIServer::init() {
  assert(kvdb_ != nullptr);
  
  // 注册方法名称
  gHandleFunctions["get"]  = kv_handle_get;
  gHandleFunctions["ping"] = kv_handle_ping;

  evbase_ = event_base_new();
  htp_    = evhtp_new(evbase_, NULL);
  
  int rc;
  evhtp_set_cb(htp_, "/",   cb_root, NULL);
  evhtp_set_cb(htp_, "/kv", cb_kv,  NULL);
  
  if (nThreads_ > 1 && nThreads_ < 128) {
    evhtp_use_threads(htp_, NULL, nThreads_, NULL);
  }
  rc = evhtp_bind_socket(htp_, listenHost_.c_str(), listenPort_, 1024);
  if (rc == -1) {
    THROW_EXCEPTION_DBEX("bind socket failure, host: %s, port: %d",
                         listenHost_.c_str(), listenPort_);
  }
  
  {
    // for test
    kvdb_->set("test01", Strings::Format("value%lld", Time::CurrentTimeMill()));
    kvdb_->set("test02", Strings::Format("value%lld", Time::CurrentTimeMill()));
    kvdb_->set("test03", Strings::Format("value%lld", Time::CurrentTimeMill()));
  }
}

void APIServer::run() {
  assert(evbase_ != nullptr);
  event_base_loop(evbase_, 0);  // while(1)
}

void APIServer::stop() {
  if (evbase_ == nullptr) { return; }

  // stop event loop
  event_base_loopexit(evbase_, NULL);
  
  // release resources
  evhtp_unbind_socket(htp_);
  evhtp_free(htp_);
  event_base_free(evbase_);
}
