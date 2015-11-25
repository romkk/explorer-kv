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

void
testcb(evhtp_request_t * req, void * a) {
  evbuffer_add_reference(req->buffer_out, "foobar", 6, NULL, NULL);
  evhtp_send_reply(req, EVHTP_RES_OK);
}

int
main(int argc, char ** argv) {
  evbase_t * evbase = event_base_new();
  evhtp_t  * htp    = evhtp_new(evbase, NULL);

  evhtp_set_cb(htp, "/test", testcb, NULL);
  evhtp_bind_socket(htp, "0.0.0.0", 8080, 1024);
  event_base_loop(evbase, 0);
  return 0;
}