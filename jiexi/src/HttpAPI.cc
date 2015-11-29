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

#include "bitcoin/uint256.h"
#include "bitcoin/base58.h"
#include "bitcoin/core.h"
#include "bitcoin/util.h"

typedef void (*handleFunction)(evhtp_request_t *req, const vector<string> &params, const string &queryId);

// global vars
KVDB *gDB = nullptr;
std::unordered_map<string, handleFunction> gHandleFunctions;  // kv handle functions
APIHandler *gAPIHandler = nullptr;


//////////////////////////////// static functions //////////////////////////////
void cb_root(evhtp_request_t * req, void *ptr);
void cb_kv(evhtp_request_t *req, void *ptr);

void kv_handle_ping(evhtp_request_t *req, const vector<string> &params, const string &queryId);
void kv_handle_get(evhtp_request_t *req, const vector<string> &params, const string &queryId);

void dbGetKeys(const vector<string> &keys, APIResponse &resp);
void kv_output(evhtp_request_t *req, const string &queryId, const APIResponse &resp,
               const int32_t error_no = API_ERROR_SUCCESS, const char *error_msg = nullptr);

void api_output(evhtp_request_t * req, const string &data,
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


////////////////////////////////// kv functions ///////////////////////////////////

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

////////////////////////////////// api functions ///////////////////////////////////
void api_output(evhtp_request_t *req, const string &data,
                const int32_t error_no, const char *error_msg) {
  string output = Strings::Format("{\"error_no\":%d,\"error_msg\":\"%s\",\"data\":",
                                  error_no, error_msg ? error_msg : "");
  evbuffer_add_reference(req->buffer_out, output.c_str(), output.length(), NULL, NULL);
  if (data.length() == 0) {
    evbuffer_add_reference(req->buffer_out, "null", 4, NULL, NULL);
  } else {
    evbuffer_add_reference(req->buffer_out, data.c_str(), data.length(), NULL, NULL);
  }
  evbuffer_add_reference(req->buffer_out, "}", 1, NULL, NULL);
  evhtp_send_reply(req, error_no == API_ERROR_SUCCESS ? EVHTP_RES_OK : EVHTP_RES_400);
}

inline
void api_output_error(evhtp_request_t *req, const int32_t error_no, const char *error_msg) {
  api_output(req, "", error_no, error_msg);
}


////////////////////////////// evhtp callback function //////////////////////////

void cb_root(evhtp_request_t * req, void *ptr) {
  string s = Strings::Format("btc.com Explorer API server, %s (UTC+0)", date("%F %T").c_str());
  evbuffer_add_reference(req->buffer_out, s.c_str(), s.length(), NULL, NULL);
  evhtp_send_reply(req, EVHTP_RES_OK);
}

void cb_api(evhtp_request_t *req, void *ptr) {
  // method
  const char *method = evhtp_kv_find(req->uri->query, "method");
  if (method == nullptr) {
    api_output_error(req, API_ERROR_EMPTY_PARAMS, "method is empty");
    return;
  }
  
  // verbose
  int32_t verbose = 1;
  if (evhtp_kv_find(req->uri->query, "verbose") != nullptr) {
    verbose = atoi(evhtp_kv_find(req->uri->query, "verbose"));
  }
  if (verbose <= 0) {
    verbose = 1;
  }
  
  // page_no
  int32_t pageNo = 1;
  if (evhtp_kv_find(req->uri->query, "page_no") != nullptr) {
    pageNo = atoi(evhtp_kv_find(req->uri->query, "page_no"));
  }
  if (pageNo <= 0) {
    pageNo = 1;
  }
  
  // page_size
  int32_t pageSize = 50;
  if (evhtp_kv_find(req->uri->query, "page_size") != nullptr) {
    pageSize = atoi(evhtp_kv_find(req->uri->query, "page_size"));
  }
  if (pageSize < 0 || pageSize > 1000) {
    pageSize = 50;
  }
  
  APIInOut apiInOut(verbose, pageNo, pageSize);
  if (strcmp(method, "address") == 0) {
    string data;
    gAPIHandler->address(apiInOut, req);
  }
  
  // output
  api_output(req, apiInOut.data_, apiInOut.errorNo_, apiInOut.errorMsg_.c_str());
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

/////////////////////////////////// APIHandler //////////////////////////////////

APIInOut::APIInOut(int32_t verbose, int32_t pageNo, int32_t pageSize):
verbose_(verbose), pageNo_(pageNo), pageSize_(pageSize), errorNo_(0) {
}

/////////////////////////////////// APIHandler //////////////////////////////////

APIHandler::APIHandler(KVDB *kvdb): kvdb_(kvdb) {
}

void APIHandler::address(APIInOut &resp, evhtp_request_t *req) {
  string address;
  string kvKey;
  string kvValue;
  
  if (evhtp_kv_find(req->uri->query, "address") != nullptr) {
    address = string(evhtp_kv_find(req->uri->query, "address"));
  }
  
  CBitcoinAddress addressObj(address);
  CKeyID keyID;
  if (!addressObj.IsValid()) {
    resp.errorNo_  = API_ERROR_INVALID_PARAMS;
    resp.errorMsg_ = "invalid address";
    return;
  }
  addressObj.GetKeyID(keyID);

  // address object
  kvKey = Strings::Format("%s%s", KVDB_PREFIX_ADDR_OBJECT, address.c_str());
  bool isExist = false;
  const fbe::Address *fbAddress = nullptr;
  
  kvdb_->getMayNotExist(kvKey, kvValue);
  if (kvValue.size() != 0) {
    fbAddress = flatbuffers::GetRoot<fbe::Address>(kvValue.data());
    isExist = true;
  }
  
  string &buf = resp.data_;
  buf.append("{");
  buf.append(Strings::Format("\"address\":\"%s\",", address.c_str()));
  buf.append(Strings::Format("\"hash160\":\"%s\",", keyID.ToString().c_str()));
  buf.append(Strings::Format("\"n_tx\":%d,",             isExist ? fbAddress->tx_count() : 0));
  buf.append(Strings::Format("\"total_received\":%lld,", isExist ? fbAddress->received() : 0));
  buf.append(Strings::Format("\"total_sent\":%lld,",     isExist ? fbAddress->sent()     : 0));
  buf.append(Strings::Format("\"final_balance\":%lld,",  isExist ? (fbAddress->received() - fbAddress->sent()) : 0));
  buf.append("\"txs\":[");
  
  // get txs
  if (fbAddress != nullptr && fbAddress->tx_count() > 0) {
    int32_t endIdx = fbAddress->tx_count() - 1 - resp.pageSize_;
    if (endIdx <= 0) {
      endIdx = 0;
    }
    const string keyStart = Strings::Format("%s%s_%010d", KVDB_PREFIX_ADDR_TX,
                                            address.c_str(), fbAddress->tx_count() - 1);
    const string keyEnd   = Strings::Format("%s%s_%010d", KVDB_PREFIX_ADDR_TX,
                                            address.c_str(), endIdx);
    vector<string> keys;
    vector<string> values;
    kvdb_->range(keyStart, keyEnd, resp.pageSize_, keys, values);
    assert(values.size() > 0);
    for (const auto &value : values) {
      auto fbAddressTx = flatbuffers::GetRoot<fbe::AddressTx>(value.data());
      buf.append("{");
      buf.append(Strings::Format("\"hash\":\"%s\",", fbAddressTx->tx_hash()->c_str()));
      buf.append(Strings::Format("\"height\":%d,",   fbAddressTx->tx_height()));
      buf.append(Strings::Format("\"ymd\":%d,",      fbAddressTx->ymd()));
      buf.append(Strings::Format("\"balance_diff\":%lld,", fbAddressTx->balance_diff()));
      getTx(fbAddressTx->tx_hash()->str(), buf, resp.verbose_);
      buf.append("},");
    }
    removeLastComma(buf);
  }
  buf.append("]");  // /txs
  buf.append("}");
}

void APIHandler::getTx(const string &txHash, string &buf, int32_t verbose) {
  string kvKey = Strings::Format("%s%s", KVDB_PREFIX_TX_OBJECT, txHash.c_str());
  string kvValue;
  
  kvdb_->get(kvKey, kvValue);
  auto fbTx = flatbuffers::GetRoot<fbe::Tx>(kvValue.data());
  buf.append(Strings::Format("\"hash\":\"%s\",", txHash.c_str()));
  buf.append(Strings::Format("\"block_height\":%d,", fbTx->height()));
  buf.append(Strings::Format("\"is_coinbase\":%s,",  fbTx->is_coinbase() ? "true" : "false"));
  buf.append(Strings::Format("\"version\":%d,",      fbTx->version()));
  buf.append(Strings::Format("\"lock_time\":%u,",    fbTx->lock_time()));
  buf.append(Strings::Format("\"size\":%d,",         fbTx->size()));
  buf.append(Strings::Format("\"fee\":%lld,",        fbTx->fee()));
  buf.append(Strings::Format("\"inputs_count\":%d,",    fbTx->inputs_count()));
  buf.append(Strings::Format("\"inputs_value\":%lld,",  fbTx->inputs_value()));
  buf.append(Strings::Format("\"outputs_count\":%d,",   fbTx->outputs_count()));
  buf.append(Strings::Format("\"outputs_value\":%lld,", fbTx->outputs_value()));
  
  //
  // inputs
  //
  buf.append("\"inputs\":[");
  for (auto input : *fbTx->inputs()) {
    buf.append("{");
    buf.append(Strings::Format("\"script_asm\":\"%s\",", input->script_asm()->c_str()));
    buf.append(Strings::Format("\"script_hex\":\"%s\",", input->script_hex()->c_str()));
    buf.append(Strings::Format("\"sequence\":%u,", (uint32_t)input->sequence()));
    
    if (!fbTx->is_coinbase()) {
      buf.append(Strings::Format("\"prev_hash\":\"%s\",", input->prev_tx_hash()->c_str()));
      buf.append(Strings::Format("\"prev_position\":%d,", input->prev_position()));
      buf.append(Strings::Format("\"prev_value\":%lld,",  input->prev_value()));
      buf.append("\"prev_addr\":[");
      if (input->prev_addresses()->size()) {
        for (auto addr : *input->prev_addresses()) {
          buf.append(Strings::Format("\"%s\"", addr->c_str()));
        }
      }
      removeLastComma(buf);
      buf.append("]");
    }
    removeLastComma(buf);
    buf.append("},");
  }
  removeLastComma(buf);
  buf.append("],");  // /inputs
  
  //
  // outputs
  //
  buf.append("\"outputs\":[");
  for (auto output : *fbTx->outputs()) {
    buf.append("{");
    // address
    buf.append("\"address\":[");
    if (output->addresses()->size()) {
      for (auto addr : *output->addresses()) {
        buf.append(Strings::Format("\"%s\"", addr->c_str()));
      }
    }
    removeLastComma(buf);
    buf.append("],");
    
    buf.append(Strings::Format("\"value\":%lld,",         output->value()));
    buf.append(Strings::Format("\"script_asm\":\"%s\",",  output->script_asm()->c_str()));
    buf.append(Strings::Format("\"script_hex\":\"%s\",",  output->script_hex()->c_str()));
    buf.append(Strings::Format("\"script_type\":\"%s\",", output->script_type()->c_str()));
    
    removeLastComma(buf);
    buf.append("},");
  }
  removeLastComma(buf);
  buf.append("]");  // /outputs
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
  
  // 设置
  gAPIHandler = new APIHandler(kvdb_);

  evbase_ = event_base_new();
  htp_    = evhtp_new(evbase_, NULL);
  
  int rc;
  evhtp_set_cb(htp_, "/",   cb_root, NULL);
  evhtp_set_cb(htp_, "/kv",   cb_kv,   NULL);
  evhtp_set_cb(htp_, "/kv/",  cb_kv,   NULL);
  evhtp_set_cb(htp_, "/api",  cb_api,  NULL);
  evhtp_set_cb(htp_, "/api/", cb_api,  NULL);
  
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


