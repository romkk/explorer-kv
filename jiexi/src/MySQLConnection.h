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

#ifndef MYSQLCONNECTION_H_
#define MYSQLCONNECTION_H_

#include "Common.h"

extern "C" struct st_mysql;
typedef struct st_mysql MYSQL;
extern "C" struct st_mysql_res;
typedef struct st_mysql_res MYSQL_RES;

/**
 * Simple wrapper for MYSQL_RES
 * auto free
 */
struct MySQLResult {
  struct st_mysql_res * result;
  MySQLResult();
  MySQLResult(MYSQL_RES * result);
  void reset(MYSQL_RES * result);
  ~MySQLResult();
  uint64 numRows();
  uint32 fields();
  char ** nextRow();
};


/**
 * Simple wrapper for MYSQL connection
 * open/close auto managed by class
 * with support for auto-reconnect
 * @see test/TestMySQLConnection.cc for demo usage
 */
class MySQLConnection {
protected:
  string uriStr;
  struct st_mysql * conn;

public:
  MySQLConnection(const string & uriStr);

  ~MySQLConnection();

  void open();

  void close();

  bool ping();

  bool execute(const char * sql);
  
  bool query(const char * sql, MySQLResult & result);

  bool query(const string & sql, MySQLResult & result) {
    return query(sql.c_str(), result);
  }

  uint64 update(const char * sql);
  uint64 updateOrThrowEx(const char * sql, const int32_t affectedRows=0);

  uint64 update(const string & sql) {
    return update(sql.c_str());
  }
  uint64 updateOrThrowEx(const string &sql, const int32_t affectedRows=0) {
    return updateOrThrowEx(sql.c_str(), affectedRows);
  }
  uint64 affectedRows();
  uint64 getInsertId();

  string getVariable(const char *name);
};

#endif /* MYSQLCONNECTION_H_ */
