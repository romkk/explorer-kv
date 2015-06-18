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

#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <err.h>
#include <errno.h>
#include <unistd.h>

#include <boost/filesystem.hpp>

#include "../bitcoin/chainparams.h"

#include "Common.h"
#include "Parser.h"

Parser *gParser = nullptr;

void handler(int sig) {
  if (gParser) {
    gParser->stop();
  }
}

void usage() {
  string u = "Usage:\n\n";
  u += "Normal:\n\ttparser -c \"tparser.conf\" -l \"tparser.log\"\n";
  u += "Reverse:\n\ttparser -c \"tparser.conf\" -l \"tparser.log\" -r 12345678\n";
  u += "\targs:\n";
  u += "\t\t-r <stop txlog ID>   INT, stop txlog ID will be executed\n";
  fprintf(stderr, u.c_str());
}

int main(int argc, char **argv) {
  char *optLog  = NULL;
  char *optConf = NULL;
  int64_t reverseEndID = 0;
  FILE *fdLog   = NULL;
  int c;

  if (argc <= 1) {
    usage();
    return 1;
  }
  while ((c = getopt(argc, argv, "c:l:h")) != -1) {
    switch (c) {
      case 'c':
        optConf = optarg;
        break;
      case 'l':
        optLog = optarg;
        break;
      case 'r':
        reverseEndID = atoi64(optarg);
        break;
      case 'h': default:
        usage();
        exit(0);
    }
  }

  // write pid to file
  writePid2FileOrExit("tparser.pid");

  fdLog = fopen(optLog, "a");
  if (!fdLog) {
    fprintf(stderr, "can't open file: %s\n", optLog);
    exit(1);
  }
  Log::SetDevice(fdLog);

  LOG_INFO("---------------------- tparser start ----------------------");
  if (!boost::filesystem::is_regular_file(optConf)) {
    LOG_FATAL("can't find config file: %s", optConf);
    exit(1);
  }
  Config::GConfig.parseConfig(optConf);

  // check testnet
  if (Config::GConfig.getBool("testnet", false)) {
    SelectParams(CChainParams::TESTNET);
    LOG_WARN("using bitcoin testnet");
  } else {
    SelectParams(CChainParams::MAIN);
  }

  // set log level
  if (IsDebug()) {
    Log::SetLevel(LOG_LEVEL_DEBUG);
  } else {
    Log::SetLevel((LogLevel)Config::GConfig.getInt("log.level", LOG_LEVEL_INFO));
  }

  signal(SIGTERM, handler);
  signal(SIGINT,  handler);

  gParser = new Parser();
  if (!gParser->init()) {
    LOG_FATAL("Parser init failed");
    exit(1);
  }

  if (reverseEndID > 0) {
    string l = Strings::Format("using reverse mode, end txlog ID: %lld",
                               reverseEndID);
    LOG_WARN("%s", l.c_str());
    fprintf(stdout, l.c_str());
    fprintf(stdout, "\n");
    gParser->setReverseMode(reverseEndID);
  }

  try {
    gParser->run();
    delete gParser;
    gParser = nullptr;
  } catch (std::exception & e) {
    LOG_FATAL("tparser exception: %s", e.what());
    return 1;
  }
  return 0;
}
