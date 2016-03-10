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

#include "NotifyLog.h"
#include "Util.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/file.h>

#include <boost/filesystem.hpp>

namespace fs = boost::filesystem;

static Notify *gNotify = nullptr;

/////////////////////////////////  NotifyItem  /////////////////////////////////
NotifyItem::NotifyItem() {
  reset();
}

void NotifyItem::loadtx(const int32_t type, const string &address,
                        const uint256 &hash, const int32_t height, const int64_t amount) {
  reset();
  timestamp_ = (uint32_t)time(nullptr);
  type_   = type;
  height_ = height;
  amount_ = amount;
  hash_   = hash;
  snprintf(address_, sizeof(address_), "%s", address.c_str());
}

void NotifyItem::loadblock(const int32_t type, const uint256 &hash, const int32_t height) {
  reset();
  timestamp_ = (uint32_t)time(nullptr);
  type_   = type;
  height_ = height;
  hash_   = hash;
}

void NotifyItem::reset() {
  type_ = -1;
  height_ = -1;
  amount_ = 0;
  hash_ = uint256();
  memset(address_, 0, sizeof(address_));
}

string NotifyItem::toStr() const {
  // tx
  if (type_ >= 20 && type_ <= 23) {
    return Strings::Format("%s,%d,%s,%d,%s,%lld",
                           date("%F %T", timestamp_).c_str(), type_, address_, height_,
                           hash_.ToString().c_str(), amount_);
  }
  // block
  else if (type_ >= 10 && type_ <= 11) {
    return Strings::Format("%s,%d,%d,%s",
                           date("%F %T", timestamp_).c_str(), type_, height_,
                           hash_.ToString().c_str());
  }

  // should not be here
  LOG_ERROR("[NotifyItem::toStr] unknown type");
  return "";
}

bool NotifyItem::parse(const string &line) {
  reset();

  // 按照 ',' 切分
  const vector<string> arr = split(line, ',', 6);

  timestamp_ = (uint32_t)str2time(arr[0].c_str(), "%F %T");
  type_ = atoi(arr[1].c_str());

  // tx
  if (type_ >= 20 && type_ <= 23) {
    assert(arr.size() == 6);
    snprintf(address_, sizeof(address_), "%s", arr[2].c_str());
    height_ = atoi(arr[3].c_str());
    hash_ = uint256(arr[4].c_str());
    amount_ = atoll(arr[5].c_str());
    return true;
  }

  // block
  if (type_ >= 10 && type_ <= 11) {
    assert(arr.size() == 4);
    height_ = atoi(arr[2].c_str());
    hash_ = uint256(arr[3].c_str());
    return true;
  }

  // should not be here
  LOG_ERROR("[NotifyItem::parse] unknown type");
  return false;
}

/////////////////////////////////  NotifyHttpd  /////////////////////////////////
void cb_address(evhtp_request_t * req, void * a) {
  static string token = Config::GConfig.get("notification.httpd.token");
  static string successResp = "{\"error_no\":0,\"error_msg\":\"\"}";

  string resp;
  const char *queryToken   = evhtp_kv_find(req->uri->query, "token");
  const char *queryAppID   = evhtp_kv_find(req->uri->query, "appid");
  const char *queryMethod  = evhtp_kv_find(req->uri->query, "method");
  const char *queryAddress = evhtp_kv_find(req->uri->query, "address");
  const int32_t appID = atoi(queryAppID);

  do {
    // invalid token
    if (strcmp(queryToken, token.c_str()) != 0) {
      resp = "{\"error_no\":10,\"error_msg\":\"invalid token\"}";
      break;
    }

    // invalid appID
    if (appID <= 0) {
      resp = "{\"error_no\":11,\"error_msg\":\"invalid appid\"}";
      break;
    }

    // insert address
    if (strcmp(queryMethod, "insert") == 0) {
      if (!gNotify->insertAddress(appID, queryAddress)) {
        resp = "{\"error_no\":20,\"error_msg\":\"invalid address or already exist\"}";
      } else {
        resp = successResp;
      }
      break;
    }

    // remove address
    if (strcmp(queryMethod, "delete") == 0) {
      if (!gNotify->removeAddress(appID, queryAddress)) {
        resp = "{\"error_no\":21,\"error_msg\":\"invalid address or not exist\"}";
      } else {
        resp = successResp;
      }
      break;
    }

    resp = "{\"error_no\":12,\"error_msg\":\"invalid method\"}";
  } while(0);

  evbuffer_add(req->buffer_out, resp.c_str(), resp.length());
  evhtp_send_reply(req, EVHTP_RES_OK);
}

NotifyHttpd::NotifyHttpd(): running_(false), evbase_(nullptr), htp_(nullptr), nThreads_(4)
{
  listenHost_ = Config::GConfig.get("notification.httpd.host");
  listenPort_ = (int32_t)Config::GConfig.getInt("notification.httpd.port");
}

NotifyHttpd::~NotifyHttpd() {
}

void NotifyHttpd::init() {
  evbase_ = event_base_new();
  htp_    = evhtp_new(evbase_, NULL);

  int rc;
  evhtp_set_cb(htp_, "/address",  cb_address, NULL);
  evhtp_set_cb(htp_, "/address/", cb_address, NULL);

  evhtp_use_threads(htp_, NULL, nThreads_, NULL);

  rc = evhtp_bind_socket(htp_, listenHost_.c_str(), listenPort_, 1024);
  if (rc == -1) {
    THROW_EXCEPTION_DBEX("bind socket failure, host: %s, port: %d",
                         listenHost_.c_str(), listenPort_);
  }
}

void NotifyHttpd::setNotifyHandler(Notify *notify) {
  gNotify = notify;
}

void NotifyHttpd::run() {
  assert(evbase_ != nullptr);
  running_ = true;

  event_base_dispatch(evbase_);
}

void NotifyHttpd::stop() {
  LogScope ls("APIServer::stop");

  if (!running_) { return; }
  running_ = false;

  if (evbase_ == nullptr) { return; }

  // stop event loop
  event_base_loopbreak(evbase_);

  // release resources
  evhtp_unbind_socket(htp_);
  evhtp_free(htp_);
  event_base_free(evbase_);

  evbase_ = nullptr;
  htp_    = nullptr;
}



/////////////////////////////////  NotifyProducer  /////////////////////////////////

NotifyProducer::NotifyProducer(const string &dir):
  lockFd_(-1), fileIndex_(-1), fileOffset_(-1L),
  fileHandler_(nullptr), kFileMaxSize_(100 * 1024 * 1024)
{
  // dir_
  dir_ = dir;

  // 通知文件
  inotifyFile_ = dir_ + "/NOTIFY_TPARSER_TO_NOTIFYD";
  {
    FILE *f = fopen(inotifyFile_.c_str(), "w");
    if (f == nullptr) {
      THROW_EXCEPTION_DBEX("create file fail: %s", inotifyFile_.c_str());
    }
    fclose(f);
  }
}

NotifyProducer::~NotifyProducer() {
  if (lockFd_ != -1) {
    flock(lockFd_, LOCK_UN);
    close(lockFd_);
    lockFd_ = -1;
  }

  if (fileHandler_ != nullptr) {
    fsync(fileno(fileHandler_));
    fclose(fileHandler_);
    fileHandler_ = nullptr;
  }
}

void NotifyProducer::init() {
  // 加锁LOCK
  {
    const string lockFile = Strings::Format("%s/LOCK", dir_.c_str());
    lockFd_ = open(lockFile.c_str(), O_RDWR|O_CREAT|O_TRUNC, 0644);
    if (flock(lockFd_, LOCK_EX) != 0) {
      LOG_FATAL("can't lock file: %s", lockFile.c_str());
    }
  }

  //
  // 遍历所有数据文件，找出最近的文件，和最近的记录
  //
  const string filesDir = Strings::Format("%s/files", dir_.c_str());
  fs::path filesPath(filesDir);
  tryCreateDirectory(filesPath);
  {
    for (fs::directory_iterator end, it(filesPath); it != end; ++it) {
      const int idx = atoi(it->path().stem().c_str());
      if (idx > fileIndex_) {
        fileIndex_ = idx;
      }
    }
  }
  LOG_INFO("begin file index: %d", fileIndex_);
}


void NotifyProducer::write(const string &lines) {
  //
  // 写日志
  //
  if (fileIndex_ == -1) {
    fileIndex_ = 0;  // reset to zero as begin index
  }
  if (fileHandler_ == nullptr) {
    const string file = Strings::Format("%s/files/%d.log", dir_.c_str(), fileIndex_);
    fileHandler_ = fopen(file.c_str(), "a");  // append mode
    if (fileHandler_ == nullptr) {
      THROW_EXCEPTION_DBEX("open file failure: %s", file.c_str());
    }
    fseek(fileHandler_, 0L, SEEK_END);
    fileOffset_ = ftell(fileHandler_);
  }

  size_t res = fwrite(lines.c_str(), 1U, lines.length(), fileHandler_);
  if (res != lines.length()) {
    THROW_EXCEPTION_DBEX("[NotifyProducer::write] fwrite return size_t(%llu) is "
                         "NOT match line length: %llu", res, lines.length());
  }
  fileOffset_ += lines.length();
  fflush(fileHandler_);  // fwrite 后执行 fflush 保证其他程序立即可以读取到

  // 写完成后，再执行通知
  // 如果直接监听日志文件的 IN_MODIFY 时间，可能读取不到完整的一行，未写完
  // 就触发了 IN_MODIFY 时间，所以用单独文件去触发通知
  {
    //
    // 只读打开后就关闭掉，会产生一个通知事件，由其他程序捕获
    //     IN_CLOSE_NOWRITE: 一个以只读方式打开的文件或目录被关闭。
    //
    FILE *f = fopen(inotifyFile_.c_str(), "r");
    assert(f != nullptr);
    fclose(f);
  }

  //
  // 切换日志：超过最大文件长度则关闭文件，下次写入时会自动打开新的文件
  //
  if (fileOffset_ > kFileMaxSize_) {
    fsync(fileno(fileHandler_));
    fclose(fileHandler_);
    fileHandler_ = nullptr;
    fileIndex_++;
    LOG_INFO("log1's size(%lld) reach max(%lld), switch to new file index: %d",
             fileOffset_, kFileMaxSize_, fileIndex_);
  }
}


///////////////////////////////////  Notify  ///////////////////////////////////
Notify::Notify(): running_(false),
logDir_(Config::GConfig.get("notification.dir")),
iNotifyFile_(Config::GConfig.get("notification.inotify.file")),
db_(Config::GConfig.get("db.notifyd.uri")), logReader_(logDir_)
{
}

Notify::~Notify() {
  changed_.notify_all();

  if (threadWatchNotify_.joinable()) {
    threadWatchNotify_.join();
  }
  if (threadConsumeNotifyLogs_.joinable()) {
    threadConsumeNotifyLogs_.join();
  }
}

void Notify::init() {
  // load address
  loadAddressTableFromDB();

  // get last file status: index & offset
  getStatus();
}

void Notify::setup() {
  running_ = true;

  // setup threads
  threadWatchNotify_       = thread(&Notify::threadWatchNotify, this);
  threadConsumeNotifyLogs_ = thread(&Notify::threadConsumeNotifyLogs, this);
}

void Notify::stop() {
  LOG_INFO("stop notifyd...");
  running_ = false;
  if (!running_) { return; }

  inotify_.RemoveAll();
  changed_.notify_all();
}

void Notify::loadAddressTableFromDB() {
  MySQLResult res;
  char **row;
  string sql;

  sql = "SELECT `app_id`,`address` FROM `app_subcribe`";
  db_.query(sql, res);
  if (res.numRows() == 0) {
    LOG_WARN("empty table.app_subcribe");
    return;
  }

  LOG_INFO("load %llu from table.app_subcribe", res.numRows());
  while ((row = res.nextRow()) != nullptr) {
    insertAddress(atoi(row[0]), row[1], false);
  }
}

bool Notify::insertAddress(const int32_t appID, const char *address, bool sync2mysql) {
  ScopeLock sl(lock_);
  CBitcoinAddress bitcoinAddr(address);
  if (!bitcoinAddr.IsValid()) {
    return false;
  }
  const string a = bitcoinAddr.ToString();

  if (addressTable_.find(a) != addressTable_.end() &&
      addressTable_[a].find(appID) != addressTable_[a].end()) {
    return false;  // already exist
  }

  // insert to memory first than insert to msyql
  addressTable_[a].insert(appID);

  const bool isAppIDExist = (appIds_.find(appID) != appIds_.end());
  if (!isAppIDExist) {
    appIds_.insert(appID);
  }

  // 首次从数据库加载时，无需执行下面部分
  if (sync2mysql) {
    string sql;
    MySQLResult res;

    // create table
    if (!isAppIDExist) {
      const string tName = Strings::Format("event_app_%d", appID);
      sql = Strings::Format("SHOW TABLES LIKE '%s'", tName.c_str());
      db_.query(sql, res);

      if (res.numRows() == 0) {
      	sql = Strings::Format("CREATE TABLE `%s` LIKE `meta_event_app`", tName.c_str());
	      db_.updateOrThrowEx(sql);
      }
    }

    // insert into db
    const string now = date("%F %T");
    sql = Strings::Format("INSERT INTO `app_subcribe` (`app_id`, `address`, "
                          " `created_at`, `updated_at`) VALUES (%d, '%s', '%s', '%s');",
                          appID, a.c_str(), now.c_str(), now.c_str());
    db_.updateOrThrowEx(sql, 1);
  }

  return true;
}

bool Notify::removeAddress(const int32_t appID, const char *address) {
  ScopeLock sl(lock_);
  CBitcoinAddress bitcoinAddr(address);
  if (!bitcoinAddr.IsValid()) {
    return false;
  }
  const string a = bitcoinAddr.ToString();

  if (addressTable_.find(a) == addressTable_.end() ||
      addressTable_[a].find(appID) == addressTable_[a].end()) {
    return false;  // not exist
  }

  // remove from memory first
  addressTable_[string(address)].erase(appID);

  // remove from mysql
  string sql = Strings::Format("DELETE FROM `app_subcribe` WHERE `app_id`=%d AND `address`='%s'",
                               appID, a.c_str());
  db_.updateOrThrowEx(sql, 1);

  return true;
}

void Notify::threadWatchNotify() {
  try {
    //
    // IN_CLOSE_NOWRITE :
    //     一个以只读方式打开的文件或目录被关闭
    //     A file or directory that had been open read-only was closed.
    // `cat FILE` 可触发该事件
    //
    InotifyWatch watch(iNotifyFile_, IN_CLOSE_NOWRITE);
    inotify_.Add(watch);
    LOG_INFO("watching notify file: %s", iNotifyFile_.c_str());

    while (running_) {
      inotify_.WaitForEvents();

      size_t count = inotify_.GetEventCount();
      while (count > 0) {
        InotifyEvent event;
        bool got_event = inotify_.GetEvent(&event);

        if (got_event) {
          string mask_str;
          event.DumpTypes(mask_str);
          LOG_DEBUG("get inotify event, mask: %s", mask_str.c_str());
        }
        count--;

        // notify other threads
        changed_.notify_all();
      }
    } /* /while */
  } catch (InotifyException &e) {
    THROW_EXCEPTION_DBEX("Inotify exception occured: %s", e.GetMessage().c_str());
  }
}

void Notify::threadConsumeNotifyLogs() {
  vector<string>  lines;
  vector<int64_t> offsets;

  while (running_) {
    // 必须在读之前检测是否存在新文件，否则可能漏读
    bool isNewFileExist = logReader_.isNewFileExist(logFileIndex_);

    // 读取日志
    logReader_.readLines(logFileIndex_, logFileOffset_, &lines, &offsets);
    if (!running_) { break; }

    // 已经存在新文件的情况下，未读取到，则表明需进行文件切换
    if (isNewFileExist && lines.size() == 0) {
      logFileIndex_++;
      logFileOffset_ = 0;

      continue;
    }

    if (lines.size() == 0) {
      UniqueLock ul(lock_);
      // 默认等待N毫秒，直至超时，中间有人触发，则立即continue读取记录
      changed_.wait_for(ul, chrono::milliseconds(10*1000));
      continue;
    }

    for (size_t i = 0; i < lines.size(); i++) {
      ScopeLock sl(lock_);

      const string &line = lines[i];
      logFileOffset_ = offsets[i];

      NotifyItem item;
      if (!item.parse(line)) {
        continue;
      }
      if (item.type_ == NOTIFY_EVENT_BLOCK_ACCEPT || item.type_ == NOTIFY_EVENT_BLOCK_REJECT) {
        // handle block event
        handleBlockEvent(item);
      } else {
        // handle tx event
        handleTxEvent(item);
      }

      updateStatus();  // update log file index & offset

    } /* /for */

  } /* /while */
}

void Notify::handleBlockEvent(NotifyItem &item) {
  string sql;

  // 遍历所有appid，分别插入至不同的表中
  for (const auto appId : appIds_) {
    const string now = date("%F %T");
    sql = Strings::Format("INSERT INTO `event_app_%d` (`type`, `hash`, `height`, "
                          "     `address`, `amount`, `created_at`, `updated_at`) "
                          " VALUES (%d, '%s', %d, '', 0, '%s', '%s');",
                          appId,
                          item.type_, item.hash_.ToString().c_str(), item.height_,
                          now.c_str(), now.c_str());
    db_.updateOrThrowEx(sql, 1);
  }
}

void Notify::handleTxEvent(NotifyItem &item) {
  string sql;

  const auto it = addressTable_.find(string(item.address_));
  if (it == addressTable_.end()) {
    return;
  }

  for (const auto appId : it->second) {
    const string now = date("%F %T");
    sql = Strings::Format("INSERT INTO `event_app_%d` (`type`, `hash`, `height`, "
                          "     `address`, `amount`, `created_at`, `updated_at`) "
                          " VALUES (%d, '%s', %d, '%s', %lld, '%s', '%s');",
                          appId,
                          item.type_, item.hash_.ToString().c_str(), item.height_,
                          item.address_, item.amount_,
                          now.c_str(), now.c_str());
    db_.updateOrThrowEx(sql, 1);
  }
}

void Notify::updateStatus() {
  string sql;

  // INSERT INTO `meta_notifyd` (`key`, `value`, `created_at`, `updated_at`)
  sql = Strings::Format("UPDATE `meta_notifyd` SET `value`='%d,%lld' "
                        " WHERE `key`='notify.file.status' ",
                        logFileIndex_, logFileOffset_);
  db_.updateOrThrowEx(sql, 1);
}

void Notify::getStatus() {
  char **row;
  MySQLResult res;
  string sql;
  sql = "SELECT `value` FROM `meta_notifyd` WHERE `key`='notify.file.status' ";
  db_.query(sql, res);

  if (res.numRows() == 0) {
    // not exist
    const string now = date("%F %T");
    sql = Strings::Format("INSERT INTO `meta_notifyd` (`key`, `value`, `created_at`, `updated_at`) "
                          "VALUES ('notify.file.status', '0,0', '%s', '%s') ",
                          now.c_str(), now.c_str());
    db_.updateOrThrowEx(sql, 1);
    logFileIndex_  = 0;
    logFileOffset_ = 0;
  }
  else
  {
    // existed, get value
    row = res.nextRow();
    const string value = string(row[0]);
    vector<string> arr = split(value, ',');
    assert(arr.size() == 2);
    logFileIndex_  = atoi(arr[0].c_str());
    logFileOffset_ = atoll(arr[1].c_str());
  }
}


/////////////////////////////////  NotifyItem  /////////////////////////////////
NotifyLogReader::NotifyLogReader(string &logDir):
//fileIndex_(0), fileOffset_(0),
fileHandler_(nullptr), logDir_(logDir)
{}

NotifyLogReader::~NotifyLogReader() {
  if (fileHandler_ != nullptr) {
    fclose(fileHandler_);
    fileHandler_ = nullptr;
  }
}

void NotifyLogReader::readLines(int32_t currFileIndex, int64_t currFileOffset,
                                vector<string> *lines, vector<int64_t> *fileOffset) {
  const string currFile = Strings::Format("%s/files/%d.log",
                                          logDir_.c_str(), currFileIndex);
  lines->clear();
  fileOffset->clear();

  //
  // 打开文件并尝试读取新行
  //
  ifstream logStream(currFile);
  if (!logStream.is_open()) {
    THROW_EXCEPTION_DBEX("open file failure: %s", currFile.c_str());
  }
  logStream.seekg(currFileOffset);
  string line;

  while (getline(logStream, line)) {  // getline()读不到内容，则会关闭 ifstream
    if (logStream.eof()) {
      // eof 表示没有遇到 \n 就抵达文件尾部了，通常意味着未完全读取一行
      // 读取完最后一行后，再读取一次，才会导致 eof() 为 true
      break;
    }

    // offset肯定是同一个文件内的，不可能跨文件
    lines->push_back(line);
    fileOffset->push_back(logStream.tellg());

    if (lines->size() > 500) {  // 每次最多处理500条日志
      break;
    }
  } /* /while */

  if (lines->size() > 0) {
    LOG_DEBUG("load notify items: %lld", lines->size());
    return;
  }
}

bool NotifyLogReader::isNewFileExist(int32_t currFileIndex) {
  const string nextFile = Strings::Format("%s/files/%d.log",
                                          logDir_.c_str(), currFileIndex + 1);
  return fs::exists(fs::path(nextFile));
}

void NotifyLogReader::tryRemoveOldFiles(int32_t currFileIndex) {
  const int32_t keepLogNum = 100;  // 最多保留文件数量
  int32_t fileIdx = currFileIndex - keepLogNum;

  // 遍历，删除所有小序列号的文件
  while (fileIdx >= 0) {
    const string file = Strings::Format("%s/files/%d.log", logDir_.c_str(), fileIdx--);
    if (!fs::exists(fs::path(file))) {
      break;
    }

    // try delete
    LOG_INFO("remove old log: %s", file.c_str());
    if (!fs::remove(fs::path(file))) {
      THROW_EXCEPTION_DBEX("remove old log failure: %s", file.c_str());
    }
  }
}
