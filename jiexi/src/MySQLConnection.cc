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
#include "MySQLConnection.h"
#include "URI.h"

#include <mysql/mysql.h>

MySQLResult::MySQLResult() :
    result(nullptr) {
}

MySQLResult::MySQLResult(MYSQL_RES * result) :
    result(result) {
}

void MySQLResult::reset(MYSQL_RES * result) {
  if (this->result) {
    mysql_free_result(this->result);
  }
  this->result = result;
}

MySQLResult::~MySQLResult() {
  if (result) {
    mysql_free_result(result);
  }
}

uint64 MySQLResult::numRows() {
  if (result) {
  	return mysql_num_rows(result);
  }
  return 0;
}

uint32 MySQLResult::fields() {
  return mysql_num_fields(result);
}

char ** MySQLResult::nextRow() {
  return mysql_fetch_row(result);
}



MySQLConnection::MySQLConnection(const string & uriStr) :
    uriStr(uriStr), conn(nullptr) {
}

MySQLConnection::~MySQLConnection() {
  close();
}

void MySQLConnection::open() {
  close();
  conn = mysql_init(NULL);
  if (!conn) {
    THROW_EXCEPTION(ENOMEM, "create MYSQL failed");
  }
  URI uri;
  if (!uri.parse(uriStr)) {
    THROW_EXCEPTION_EX(EINVAL, "invalid mysql uri: %s", uriStr.c_str());
  }
  const char * db = uri.getPath().c_str();
  if ((uri.getPath().length() > 1) && (uri.getPath()[0] == '/')) {
    db = uri.getPath().c_str() + 1;
  }
  if (mysql_real_connect(conn, uri.getHost().c_str(), uri.getUser().c_str(),
      uri.getPassword().c_str(), db, uri.getPort(), nullptr, 0) == nullptr) {
    close();
    THROW_EXCEPTION_EX(mysql_errno(conn), "mysql_real_connect failed: %s", mysql_error(conn));
  }
  if (mysql_set_character_set(conn, "utf8") != 0) {
    close();
    THROW_EXCEPTION_EX(mysql_errno(conn), "mysql_set_character_set failed: %s", mysql_error(conn));
  }
}

void MySQLConnection::close() {
  if (conn) {
    mysql_close(conn);
    conn = nullptr;
  }
}

// 可用于测试是否正常连接
bool MySQLConnection::ping() {
  if (!conn) {
    open();
  }

  if (conn) {
    // mysql_ping():
    //  Checks whether the connection to the server is working. If the connection
    //  has gone down and auto-reconnect is enabled an attempt to reconnect is
    //  made. If the connection is down and auto-reconnect is disabled,
    //  mysql_ping() returns an error.
    // Zero if the connection to the server is active. Nonzero if an error occurred.
    int res = mysql_ping(conn);
    if (res == 0) {
      return true;
    }
    uint32_t error_no = mysql_errno(conn);
    LOG_ERROR("mysql_ping() failure, error_no: %u, error_info: %s",
              error_no, mysql_error(conn));
  }
  return false;
}

bool MySQLConnection::execute(const char * sql) {
  uint32_t error_no;
  int queryTimes = 0;

  if (IsDebug() && Config::GConfig.getBool("debug.sql", true)) {
    LOG_DEBUG("[MySQLConnection::execute] SQL: %s", sql);
  }
  
query:
  if (!conn) { open(); }
  queryTimes++;
  if (mysql_query(conn, sql) == 0) {
    return true;  // exec sql success
  }

  // get mysql error
  error_no = mysql_errno(conn);
  if (error_no == 2006 || error_no == 2013) {
    LOG_ERROR("exec sql failure, error_no: %u, error_info: %s",
              error_no, mysql_error(conn));
  } else {
    // 非网络连接错误，均抛出异常
    THROW_EXCEPTION_EX(ENETDOWN,
                       "exec sql failure, error_no: %u, error_info: %s",
                       error_no, mysql_error(conn));
  }

  //
  // 需要超时类重连类型的Mysql错误, 这些错误在Mysql Server重启、网络抖动均可能出现，可通过
  // mysql_ping()修复
  //
  // 2006: MySQL server has gone away
  // 2013: Lost connection to MySQL server
  //
  if (queryTimes < 2 && (error_no == 2006 || error_no == 2013)) {
    sleep(3);
    if (mysql_ping(conn) == 0) {
      LOG_WARN("reconnect success");
    } else {
      LOG_WARN("reconnect failure, close conn and try open conn again");
      close();
    }
    goto query;
  }

  return false;
}

bool MySQLConnection::query(const char * sql, MySQLResult & result) {
  bool res = execute(sql);
  result.reset(mysql_store_result(conn));
  return res;
}

uint64 MySQLConnection::update(const char * sql) {
  execute(sql);
  return mysql_affected_rows(conn);
}

uint64 MySQLConnection::updateOrThrowEx(const char * sql, const int32_t affectedRows) {
  execute(sql);
  const int32_t r = (int32_t)mysql_affected_rows(conn);
  if (r != affectedRows) {
    THROW_EXCEPTION_EX(EIO, "update DB failure, affected rows(%d) is not expected(%d)",
                       r, affectedRows);
  }
  return r;
}

uint64 MySQLConnection::affectedRows() {
  return mysql_affected_rows(conn);
}

uint64 MySQLConnection::getInsertId() {
  return mysql_insert_id(conn);
}

//
// SQL: show variables like "max_allowed_packet"
//
//   |    Variable_name    |   Value  |
//   | max_allowed_packet  | 16777216 |
//
string MySQLConnection::getVariable(const char *name) {
  string sql = Strings::Format("SHOW VARIABLES LIKE \"%s\";", name);
  MySQLResult result;
  if (!query(sql, result) || result.numRows() == 0) {
    return "";
  }
  char **row = result.nextRow();
  LOG_DEBUG("getVariable %s=%s", row[0], row[1]);
  return string(row[1]);
}
