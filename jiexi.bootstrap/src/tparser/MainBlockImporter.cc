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
#include <boost/interprocess/sync/file_lock.hpp>

#include "../bitcoin/chainparams.h"

#include "Common.h"
#include "BlockImporter.h"

static BlockImporter *gBlockImporter = nullptr;


void handler(int sig) {
  if (gBlockImporter) {
    gBlockImporter->stop();
  }
}

void usage() {
  fprintf(stderr, "Usage:\n\tblkimport -c \"blkimport.conf\" -l \"blkimport.log\" -b BeginHeight -e EndHeight\n");
}

int main(int argc, char **argv) {
  char *optLog  = NULL;
  char *optConf = NULL;
  FILE *fdLog   = NULL;
  int32_t beginHeight = 0, endHeight = 0;
  int c;

  if (argc <= 1) {
    usage();
    return 1;
  }
  while ((c = getopt(argc, argv, "c:l:b:e:h")) != -1) {
    switch (c) {
      case 'c':
        optConf = optarg;
        break;
      case 'b':
        beginHeight = atoi(optarg);
        break;
      case 'e':
        endHeight   = atoi(optarg);
        break;
      case 'l':
        optLog = optarg;
        break;
      case 'h': default:
        usage();
        exit(0);
    }
  }

  // set log device
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

  // set log level
  if (IsDebug()) {
    Log::SetLevel(LOG_LEVEL_DEBUG);
  } else {
    Log::SetLevel((LogLevel)Config::GConfig.getInt("log.level", LOG_LEVEL_INFO));
  }

  try {
    if (beginHeight > endHeight || beginHeight < 0) {
      THROW_EXCEPTION_DBEX("invalid height: [%d, %d]", beginHeight, endHeight);
    }

    gBlockImporter = new BlockImporter(Config::GConfig.get("rawdata.dir", ""),
                                       (int32_t)Config::GConfig.getInt("importer.nthreads", 4),
                                       Config::GConfig.get("bitcoind.uri"),
                                       beginHeight, endHeight);
    gBlockImporter->run();
    delete gBlockImporter;
    gBlockImporter = nullptr;
  } catch (std::exception & e) {
    LOG_FATAL("compactdb exception: %s", e.what());
    return 1;
  }
  return 0;
}
