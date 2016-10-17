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

#include "Util.h"

#include <string>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>

#include <curl/curl.h>

static std::vector<std::string> &split(const std::string &s, const char delim,
                                       std::vector<std::string> &elems,
                                       const int32_t limit) {
  std::stringstream ss(s);
  std::string item;
  while (std::getline(ss, item, delim)) {
    elems.push_back(item);
    if (limit != -1 && elems.size() == limit) {
      break;
    }
  }
  return elems;
}

std::vector<std::string> split(const std::string &s, const char delim) {
  std::vector<std::string> elems;
  split(s, delim, elems, -1/* unlimit */);
  return elems;
}

std::vector<std::string> split(const std::string &s, const char delim,
                               const int32_t limit) {
  std::vector<std::string> elems;
  split(s, delim, elems, limit);
  return elems;
}

size_t getNumberOfLines(const string &file) {
  std::ifstream f(file);
  std::size_t linesCount = 0;
  std::string line;
  while (std::getline(f , line))
    ++linesCount;
  return linesCount;
}

struct CurlChunk {
  char *memory;
  size_t size;
};

static size_t
CurlWriteChunkCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
  size_t realsize = size * nmemb;
  struct CurlChunk *mem = (struct CurlChunk *)userp;

  mem->memory = (char *)realloc(mem->memory, mem->size + realsize + 1);
  if(mem->memory == NULL) {
    /* out of memory! */
    printf("not enough memory (realloc returned NULL)\n");
    return 0;
  }

  memcpy(&(mem->memory[mem->size]), contents, realsize);
  mem->size += realsize;
  mem->memory[mem->size] = 0;

  return realsize;
}

bool httpGET(const char *url, string &response, long timeoutMs) {
  return httpPOST(url, nullptr, nullptr, response, timeoutMs);
}

bool httpGET(const char *url, const char *userpwd,
             string &response, long timeoutMs) {
  return httpPOST(url, userpwd, nullptr, response, timeoutMs);
}

bool httpPOST(const char *url, const char *userpwd,
              const char *postData, string &response, long timeoutMs) {
  struct curl_slist *headers = NULL;
  CURLcode status;
  long code;
  CURL *curl = curl_easy_init();
  struct CurlChunk chunk;
  if (!curl) {
    return false;
  }

  chunk.memory = (char *)malloc(1);  /* will be grown as needed by the realloc above */
  chunk.size   = 0;          /* no data at this point */

  headers = curl_slist_append(headers, "content-type: text/plain;");
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

  curl_easy_setopt(curl, CURLOPT_URL, url);

  if (postData != nullptr) {
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(postData));
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS,    postData);
  }

  if (userpwd != nullptr)
    curl_easy_setopt(curl, CURLOPT_USERPWD, userpwd);

  curl_easy_setopt(curl, CURLOPT_USE_SSL, CURLUSESSL_TRY);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "curl");

  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeoutMs);

  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteChunkCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA,     (void *)&chunk);

  status = curl_easy_perform(curl);
  if (status != 0) {
    LOG_ERROR("unable to request data from: %s, error: %s", url, curl_easy_strerror(status));
    goto error;
  }

  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
  if (code != 200) {
    LOG_ERROR("server responded with code: %d", code);
    goto error;
  }

  response.assign(chunk.memory, chunk.size);

  curl_easy_cleanup(curl);
  curl_slist_free_all(headers);
  free(chunk.memory);
  return true;


error:
  if (curl)
    curl_easy_cleanup(curl);
  if (headers)
    curl_slist_free_all(headers);

  free(chunk.memory);
  return false;
}

bool bitcoindRpcCall(const char *url, const char *userpwd, const char *reqData,
                     string &response) {
  return httpPOST(url, userpwd, reqData, response, 5000/* timeout ms */);
}

