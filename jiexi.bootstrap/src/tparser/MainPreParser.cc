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

PreParser *gPreParser = nullptr;

void handler(int sig) {
  if (gPreParser) {
    gPreParser->stop();
  }
}

void usage() {
  fprintf(stderr, "Usage:\n\tpre_parser -c \"tparser.bootstrap.conf\" -l \"pre_parser.log\"\n");
}

int main(int argc, char **argv) {
  char *optLog  = NULL;
  char *optConf = NULL;
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
      case 'h': default:
        usage();
        exit(0);
    }
  }

  // write pid to file
  writePid2FileOrExit("pre_parser.pid");

  fdLog = fopen(optLog, "a");
  if (!fdLog) {
    fprintf(stderr, "can't open file: %s\n", optLog);
    exit(1);
  }
  Log::SetDevice(fdLog);

  LOG_INFO("\n\n\n\n\n\n\n\n\n\n\n\n");
  if (!boost::filesystem::is_regular_file(optConf)) {
    LOG_FATAL("can't find config file: %s", optConf);
    exit(1);
  }
  Config::GConfig.parseConfig(optConf);

  // check testnet
  if (Config::GConfig.getBool("testnet", false)) {
    SelectParams(CBaseChainParams::TESTNET);
    LOG_WARN("using bitcoin testnet");
  } else {
    SelectParams(CBaseChainParams::MAIN);
  }

  // set log level
  if (IsDebug()) {
    Log::SetLevel(LOG_LEVEL_DEBUG);
  } else {
    Log::SetLevel((LogLevel)Config::GConfig.getInt("log.level", LOG_LEVEL_INFO));
  }

  signal(SIGTERM, handler);
  signal(SIGINT,  handler);

  try {
    gPreParser = new PreParser();
    gPreParser->init();
    gPreParser->run();
    delete gPreParser;
    gPreParser = nullptr;
  } catch (std::exception & e) {
    LOG_FATAL("pre_parser exception: %s", e.what());
    return 1;
  }
  return 0;
}
