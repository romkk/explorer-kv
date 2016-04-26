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

#include "Log1Producer.h"

#include "BitcoinRpc.h"
#include "Common.h"
#include "Util.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/file.h>

#include <boost/filesystem.hpp>

namespace fs = boost::filesystem;


///////////////////////////////////  Log1  /////////////////////////////////////
Log1::Log1() {
  reset();
}

Log1::~Log1() {
}

void Log1::reset() {
  type_        = -1;
  blockHeight_ = -1;
  content_.clear();
  tx_.SetNull();
  block_.SetNull();
}

// 解析当行 log1 类型的日志，解析失败则抛出异常
void Log1::parse(const string &line) {
  reset();

  // 按照 ',' 切分，最多切三份
  const vector<string> arr1 = split(line, ',', 3);

  // type
  const int32_t type = atoi(arr1[1].c_str());

  if (type == TYPE_CLEAR_MEMTXS) {
    type_ = type;
    return;
  }

  // 最后一个按照 '|' 切分
  const vector<string> arr2 = split(arr1[2], '|');

  /* block */
  if (type == TYPE_BLOCK) {
    assert(arr2.size() == 3);
    blockHeight_ = atoi(arr2[0].c_str());
    content_     = arr2[1] + arr2[2];
    type_        = type;
  }
  /* tx accept / remove */
  else if (type == TYPE_ACCEPT_TX || type == TYPE_REMOVE_TX) {
    assert(arr2.size() == 2);
    content_ = arr2[0] + arr2[1];
    type_    = type;
  }
  else {
    THROW_EXCEPTION_DBEX("[Log1::parse] invalid log1 type(%d)", type);
  }
}

const CBlock &Log1::getBlock() {
  if (block_.IsNull()) {  // 延时解析
    const string hex = content_.substr(64);
    if (!DecodeHexBlk(block_, hex)) {
      THROW_EXCEPTION_DBEX("decode block failure, hex: %s", content_.c_str());
    }
    if (block_.GetHash().ToString() != content_.substr(0, 64)) {
      THROW_EXCEPTION_DBEX("decode block failure, hash not match, hex: %s", content_.c_str());
    }
  }
  return block_;
}

const CTransaction &Log1::getTx() {
  if (tx_.IsNull()) {  // 延时解析
    const string hex = content_.substr(64);
    if (!DecodeHexTx(tx_, hex)) {
      THROW_EXCEPTION_DBEX("decode tx failure, hex: %s", content_.c_str());
    }
    if (tx_.GetHash().ToString() != content_.substr(0, 64)) {
      THROW_EXCEPTION_DBEX("decode tx failure, hash not match, hex: %s", content_.c_str());
    }
  }
  return tx_;
}

bool Log1::isAcceptTx() {
  return type_ == TYPE_ACCEPT_TX ? true : false;
}

bool Log1::isBlock() {
  return type_ == TYPE_BLOCK ? true : false;
}

bool Log1::isClearMemtxs() {
  return type_ == TYPE_CLEAR_MEMTXS ? true : false;
}

bool Log1::isRemoveTx() {
  return type_ == TYPE_REMOVE_TX ? true : false;
}

string Log1::toString() {
  if (type_ == TYPE_ACCEPT_TX) {
    return Strings::Format("(tx: %s)", getTx().GetHash().ToString().c_str());
  }
  else if (type_ == TYPE_BLOCK) {
    return Strings::Format("(block: %d, %s)", blockHeight_,
                           getBlock().GetHash().ToString().c_str());
  }
  else if (type_ == TYPE_CLEAR_MEMTXS) {
    return "(clear mempool txs)";
  }
  return "(null)";
}

//////////////////////////////////  Chain  /////////////////////////////////////

Chain::Chain(const int32_t limit): limit_(limit)
{
}
Chain::~Chain() {}

int32_t Chain::getCurHeight() const {
  if (blocks_.size() == 0) {
    return -1;
  }
  auto it = blocks_.rbegin();
  return it->first;
}

uint256 Chain::getCurHash() const {
  if (blocks_.size() == 0) {
    return uint256();
  }
  auto it = blocks_.rbegin();
  return it->second;
}

void Chain::pushFirst(const int32_t height, const uint256 &hash) {
  if (blocks_.size() != 0) {
    THROW_EXCEPTION_DBEX("blocks_ is not empty, size: %llu", blocks_.size());
  }
  blocks_[height] = hash;
  LOG_INFO("chain push first block, height: %d, hash: %s", height, hash.ToString().c_str());
}

void Chain::push(const int32_t height, const uint256 &hash,
                 const uint256 &prevHash) {
  const int32_t curHeight = getCurHeight();
  const uint256 curHash   = getCurHash();

//  LOG_DEBUG("chain push block, height: %d, hash: %s", height, hash.ToString().c_str());

  /********************* 前进 *********************/
  if (height == curHeight + 1) {
    if (prevHash != curHash) {
      THROW_EXCEPTION_DBEX("prev hash not match curHash, cur: %s, prev: %s",
                           curHash.ToString().c_str(),
                           prevHash.ToString().c_str());
    }
    blocks_[height] = hash;
  }
  /********************* 后退 *********************/
  else if (height + 1 == curHeight) {
    if (blocks_.size() <= 3) {
      THROW_EXCEPTION_DBEX("blocks should more than 3");
    }
    // 倒数第三个的hash应该是目前后退的prev hash
    auto it = blocks_.rbegin();
    ++it;
    ++it;
    if (prevHash != it->second) {
      THROW_EXCEPTION_DBEX("prev hash not match -3 block hash, -3: %s, prev: %s",
                           it->second.ToString().c_str(),
                           prevHash.ToString().c_str());
    }
    // 移除最后一个块
    blocks_.erase(std::prev(blocks_.end()));
  }
  /********************* 异常 *********************/
  else {
    THROW_EXCEPTION_DBEX("invalid block height: %d, cur: %d", height, curHeight);
  }

  // 检查数量限制，超出后移除首个元素
  while (blocks_.size() > limit_) {
    blocks_.erase(blocks_.begin());
  }
}

size_t Chain::size() const {
  return blocks_.size();
}

void Chain::pop() {
  if (blocks_.size() == 0) { return; }
  // 移除最后一个块
  blocks_.erase(std::prev(blocks_.end()));
}

///////////////////////////////  Log1Producer  /////////////////////////////////
Log1Producer::Log1Producer() : log1LockFd_(-1), log1FileHandler_(nullptr),
  log1FileIndex_(-1), chain_(2016 * 2/* max blocks */),
  log0FileIndex_(-1), log0FileOffset_(-1), log0BeginFileLastModifyTime_(0)
{
  log1Dir_ = Config::GConfig.get("log1.dir");
  log0Dir_ = Config::GConfig.get("log0.dir");

  // remove last '/'
  if (*(std::prev(log1Dir_.end())) == '/') {
    log1Dir_.resize(log1Dir_.length() - 1);
  }
  if (*(std::prev(log0Dir_.end())) == '/') {
    log0Dir_.resize(log0Dir_.length() - 1);
  }

  log0StatusFile_ = Strings::Format("%s/LOG0_STATUS", log1Dir_.c_str());

  notifyFileLog2Producer_ = log1Dir_ + "/NOTIFY_LOG1_TO_LOG2";
  notifyFileLog0_ = log0Dir_ + "/NOTIFY_LOG1PRODUCER";

  // 创建通知文件，通知 log2producer
  {
    FILE *f = fopen(notifyFileLog2Producer_.c_str(), "w");
    if (f == nullptr) {
      THROW_EXCEPTION_DBEX("create file fail: %s", notifyFileLog2Producer_.c_str());
    }
    fclose(f);
  }
  watchNotifyThread_ = thread(&Log1Producer::threadWatchNotifyFile, this);
}

Log1Producer::~Log1Producer() {
  if (log1LockFd_ != -1) {
    flock(log1LockFd_, LOCK_UN);
    close(log1LockFd_);
    log1LockFd_ = -1;
  }

  if (log1FileHandler_ != nullptr) {
    fsync(fileno(log1FileHandler_));
    fclose(log1FileHandler_);
    log1FileHandler_ = nullptr;
  }

  changed_.notify_all();
  if (watchNotifyThread_.joinable()) {
    watchNotifyThread_.join();
  }
}

void Log1Producer::stop() {
  LOG_INFO("stop log1producer...");
  running_ = false;

  inotify_.RemoveAll();
  changed_.notify_all();
}

//
// 执行顺序是特定的，如需调整请谨慎考虑。初始化中有故障，均会抛出异常，中断程序
//
void Log1Producer::init() {
  running_ = true;

  // 检测bitcoind是否正常
  BitcoinRpc bitcoind(Config::GConfig.get("bitcoind.uri"));
  if (!bitcoind.CheckBitcoind()) {
    THROW_EXCEPTION_DBEX("bitcoind is not working or error");
  }

  // 检测 log0 是否正常
  {
    fs::path filesPath(Strings::Format("%s/files", log0Dir_.c_str()));
    tryCreateDirectory(filesPath);
    fs::directory_iterator end, it(filesPath);
    if (it == end) {
      THROW_EXCEPTION_DBEX("log0dir files are empty: %s/files", log0Dir_.c_str());
    }
  }

  //
  // 检测 log0 是否发生变化？
  // 1. 没有变化: 继续上次执行的地方，继续消费 log0 日志
  // 2. 已经变化：
  //      2.1 清理本系统txlog2的mempool，写清理指令
  //      2.2 读取 log0 的首个文件首行(必然是一个块），若本地链的高度不一致，则同步
  //      2.3 从 log0 的第二行开始进行消费
  //

  bool isLog0Changed = false;

  // 检测 log0 是否发生变化？
  const string log0BeginTimeFile0 = Strings::Format("%s/BEGIN", log0Dir_.c_str());
  string log0BeginTimeDir0;
  fileGetContents(log0BeginTimeFile0, log0BeginTimeDir0);
  LOG_INFO("log0BeginTimeFile(log0dir): %s", log0BeginTimeDir0.c_str());

  const string log0BeginTimeFile1 = Strings::Format("%s/LOG0_BEGIN_TIME", log1Dir_.c_str());
  string log0BeginTimeDir1;
  fileGetContents(log0BeginTimeFile1, log0BeginTimeDir1);
  LOG_INFO("log0BeginTimeFile(log1dir): %s", log0BeginTimeDir1.c_str());

  if (log0BeginTimeDir1 != log0BeginTimeFile0) {
    isLog0Changed = true;
    LOG_WARN("log0 files has been changed");
  }

  // 初始化 log1，构建出最近的链
  initLog1();

  if (isLog0Changed) {
    // 2. 写入一条回撤指令，会令log2producer reject掉当前所有未确认交易
    writeLog1(Log1::TYPE_CLEAR_MEMTXS, "");

    // 3. 读取 log0_dir/files/0.log 最新高度/块，利用RPC命令同步至此高度
    // log0FileIndex_, log0FileOffset_ 会更新
    syncBitcoind();

    // 4. 更新 log0 的时间, 更新 log0 idx & offset
    filePutContents(log0BeginTimeFile1, log0BeginTimeDir0);
    writeLog0IdxOffset();
  }
  else
  {
    // 继续上次的进度
    getLog0IdxOffset();
  }

  LOG_INFO("start log0file info, idx: %d, offset: %lld ", log0FileIndex_, log0FileOffset_);
}

void Log1Producer::threadWatchNotifyFile() {
  try {
    //
    // IN_CLOSE_NOWRITE :
    //     一个以只读方式打开的文件或目录被关闭
    //     A file or directory that had been open read-only was closed.
    // `cat FILE` 可触发该事件
    //
    InotifyWatch watch(notifyFileLog0_, IN_CLOSE_NOWRITE);
    inotify_.Add(watch);
    LOG_INFO("watching notify file: %s", notifyFileLog0_.c_str());

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

// 初始化 log1
void Log1Producer::initLog1() {
  LogScope ls("Log1Producer::initLog1()");

  // 加锁LOCK
  {
    const string lockFile = Strings::Format("%s/LOCK", log1Dir_.c_str());
    log1LockFd_ = open(lockFile.c_str(), O_RDWR|O_CREAT|O_TRUNC, 0644);
    if (flock(log1LockFd_, LOCK_EX) != 0) {
      LOG_FATAL("can't lock file: %s", lockFile.c_str());
    }
  }

  // 遍历Log1，找出最近的文件，和最近的记录
  std::set<int32_t> filesIdxs;  // log1所有文件
  const string filesDir = Strings::Format("%s/files", log1Dir_.c_str());
  fs::path filesPath(filesDir);
  tryCreateDirectory(filesPath);
  {
    int log1FileIndex = -1;
    for (fs::directory_iterator end, it(filesPath); it != end; ++it) {
      const int idx = atoi(it->path().stem().c_str());
      filesIdxs.insert(idx);

      if (idx > log1FileIndex) {
        log1FileIndex = idx;
      }
    }
    log1FileIndex_ = log1FileIndex;
  }
  LOG_INFO("begin log1FileIndex_: %d", log1FileIndex_);

  // 找到最后的块高度 & 哈希
  if (log1FileIndex_ == -1) {
    // 没有log1文件，则采用配置文件的参数作为起始块信息
    const int32_t beginHeight = (int32_t)Config::GConfig.getInt("log1.begin.block.height");
    uint256 beginHash = uint256();
    if (beginHeight >= 0) {
      beginHash = uint256(Config::GConfig.get("log1.begin.block.hash"));
    }
    chain_.pushFirst(beginHeight, beginHash);
  } else {
    // 利用set自动排序，从小向大遍历所有文件，重新载入块链
    // TODO: 性能优化，少读取一些log1日志文件
    for (auto fileIdx : filesIdxs) {
      ifstream fin(Strings::Format("%s/files/%d.log", log1Dir_.c_str(), fileIdx));
      string line;
      Log1 log1Item;
      while (getline(fin, line)) {
        log1Item.parse(line);
        if (!log1Item.isBlock()) { continue; }
        assert(log1Item.isBlock());
        if (chain_.size() == 0) {
          chain_.pushFirst(log1Item.blockHeight_, log1Item.getBlock().GetHash());
        } else {
          chain_.push(log1Item.blockHeight_, log1Item.getBlock().GetHash(),
                      log1Item.getBlock().hashPrevBlock);
        }
      }
    } /* /for */
  }
  assert(chain_.size() >= 1);

  LOG_INFO("log1 begin block: %d, %s", chain_.getCurHeight(),
           chain_.getCurHash().ToString().c_str());
}

//
// _bitcoind_xxxxx() 系列函数内部有重复代码，因为单独移出来后有问题，疑似 JsonNode 的Bug
// 所以，每个函数都重复了请求、解析代码
//
static string _bitcoind_getBlockHashByHeight(BitcoinRpc &bitcoind, const int32_t height) {
  const string request = Strings::Format("{\"id\":1,\"method\":\"getblockhash\",\"params\":[%d]}",
                                         height);
  string response;
  const int ret = bitcoind.jsonCall(request, response, 5000/* timeout: ms */);
  if (ret != 0) {
    THROW_EXCEPTION_DBEX("bitcoind rpc call fail, req: %s", request.c_str());
  }
  JsonNode r;
  JsonNode::parse(response.c_str(), response.c_str() + response.length(), r);
  if (r["error"].type() != Utilities::JS::type::Undefined &&
      r["error"].type() == Utilities::JS::type::Obj) {
    THROW_EXCEPTION_DBEX("bitcoind rpc call fail, code: %d, error: %s",
                         r["error"]["code"].int32(),
                         r["error"]["message"].str().c_str());
  }
  return r["result"].str();
}

static string _bitcoind_getInfo(BitcoinRpc &bitcoind) {
  const string request = Strings::Format("{\"id\":1,\"method\":\"getinfo\",\"params\":[]}");
  string response;
  const int ret = bitcoind.jsonCall(request, response, 5000/* timeout: ms */);
  if (ret != 0) {
    THROW_EXCEPTION_DBEX("bitcoind rpc call fail, req: %s", request.c_str());
  }
  JsonNode r;
  JsonNode::parse(response.c_str(), response.c_str() + response.length(), r);
  if (r["error"].type() != Utilities::JS::type::Undefined &&
      r["error"].type() == Utilities::JS::type::Obj) {
    THROW_EXCEPTION_DBEX("bitcoind rpc call fail, code: %d, error: %s",
                         r["error"]["code"].int32(),
                         r["error"]["message"].str().c_str());
  }
  return r["result"].str();
}

static void _bitcoind_getBlockByHash(BitcoinRpc &bitcoind, const string &hashStr, CBlock &block) {
  const string request = Strings::Format("{\"id\":1,\"method\":\"getblock\",\"params\":[\"%s\", false]}",
                                         hashStr.c_str());
  string response;
  const int ret = bitcoind.jsonCall(request, response, 5000/* timeout: ms */);
  if (ret != 0) {
    THROW_EXCEPTION_DBEX("bitcoind rpc call fail, req: %s", request.c_str());
  }
  vector<string> strArr = split(response, '"', 5);
  assert(strArr.size() == 5);
  if (!DecodeHexBlk(block, strArr[3])) {
    LOG_ERROR("rpc request : %s", request.c_str());
    LOG_ERROR("rpc response: %s", response.c_str());
    THROW_EXCEPTION_DBEX("decode block failure, hex: %s", strArr[3].c_str());
  }
}

static int32_t _bitcoind_getBestHeight(BitcoinRpc &bitcoind) {
  const string request = Strings::Format("{\"id\":1,\"method\":\"getinfo\",\"params\":[]}");
  string response;
  const int ret = bitcoind.jsonCall(request, response, 5000/* timeout: ms */);
  if (ret != 0) {
    THROW_EXCEPTION_DBEX("bitcoind rpc call fail, req: %s", request.c_str());
  }
  JsonNode r;
  JsonNode::parse(response.c_str(), response.c_str() + response.length(), r);
  if (r["error"].type() != Utilities::JS::type::Undefined &&
      r["error"].type() == Utilities::JS::type::Obj) {
    THROW_EXCEPTION_DBEX("bitcoind rpc call fail, code: %d, error: %s",
                         r["error"]["code"].int32(),
                         r["error"]["message"].str().c_str());
  }
  return r["result"]["blocks"].int32();
}

//static void _bitcoind_getrawtransaction(BitcoinRpc &bitcoind, const uint256 &hash, CTransaction &tx, bool verbose) {
//  const string request = Strings::Format("{\"id\":1,\"method\":\"getrawtransaction\",\"params\":[\"%s\", %d]}",
//                                         hash.ToString().c_str(), verbose ? 1 : 0);
//  string response;
//  const int ret = bitcoind.jsonCall(request, response, 5000/* timeout: ms */);
//  if (ret != 0) {
//    THROW_EXCEPTION_DBEX("bitcoind rpc call fail, req: %s", request.c_str());
//  }
//  vector<string> strArr = split(response, '"', 5);
//  assert(strArr.size() == 5);
//  if (!DecodeHexTx(tx, strArr[3])) {
//    LOG_ERROR("rpc request : %s", request.c_str());
//    LOG_ERROR("rpc response: %s", response.c_str());
//    THROW_EXCEPTION_DBEX("decode tx failure, hex: %s", strArr[3].c_str());
//  }
//}
//
//// 获取内存交易列表，并根据交易之间的关联性排好序
//static void _bitcoind_getmempool_txs(BitcoinRpc &bitcoind, vector<CTransaction> &txs) {
//  string request;
//  string response;
//  JsonNode r;
//  map<uint256, set<uint256> > allTxhash;
//
//  vector<uint256> sortedTxs;  // 按照依赖顺序已经排好序的交易
//  set<uint256> sortedTxsSet;
//
//  //
//  // getrawmempool
//  //
//  request = Strings::Format("{\"id\":1,\"method\":\"getrawmempool\",\"params\":[true]}");
//  const int ret = bitcoind.jsonCall(request, response, 5000/* timeout: ms */);
//  if (ret != 0) {
//    THROW_EXCEPTION_DBEX("bitcoind rpc call fail, req: %s", request.c_str());
//  }
//  r.reset();
//  JsonNode::parse(response.c_str(), response.c_str() + response.length(), r);
//  if (r["error"].type() != Utilities::JS::type::Undefined &&
//      r["error"].type() == Utilities::JS::type::Obj) {
//    THROW_EXCEPTION_DBEX("bitcoind rpc call fail, code: %d, error: %s",
//                         r["error"]["code"].int32(),
//                         r["error"]["message"].str().c_str());
//  }
//
//  for (auto node : r["result"].array()) {
//    const string hashStr = std::string(node.key_start(),node.key_end());
//    allTxhash[uint256(hashStr)] = set<uint256>();
//
//    for (auto dependTx : node["depends"].array()) {
//      allTxhash[uint256(hashStr)].insert(uint256(dependTx.str()));
//    }
//  }
//
//  // 循环遍历
//  // 最多循环次数： allTxhash.size()
//  while (1) {
//    const size_t lastSize = sortedTxs.size();
//
//    assert(sortedTxs.size() == sortedTxsSet.size());
//    if (sortedTxs.size() == allTxhash.size()) {
//      break;  // 所有交易均已经存放至vector中
//    }
//
//    for (auto it = allTxhash.begin(); it != allTxhash.end(); ) {
//      // alias
//      const uint256 &hash         = it->first;
//      const set<uint256> &depends = it->second;
//
//      // 没有依赖关系的直接推入
//      if (depends.size() == 0) {
//        sortedTxs.push_back(hash);
//        sortedTxsSet.insert(hash);
//        allTxhash.erase(it++);
//
//        continue;
//      }
//
//      // 判断依赖交易是否存在
//      bool allDependsExist = true;
//      for (auto dependTx : depends) {
//        if (sortedTxsSet.find(dependTx) == sortedTxsSet.end()) {
//          allDependsExist = false;
//          break;
//        }
//      }
//      if (allDependsExist) {
//        sortedTxs.push_back(hash);
//        sortedTxsSet.insert(hash);
//        allTxhash.erase(it++);
//      } else {
//        it++;
//      }
//    }
//
//    if (lastSize == sortedTxs.size()) {
//      LOG_INFO("no more txs, sorted: %llu, all: %llu", sortedTxs.size(), allTxhash.size());
//      break;
//    }
//  }
//  LOG_INFO("getrawmempool size: %llu", sortedTxs.size());
//
//  //
//  // getrawtransaction
//  //
//  txs.clear();
//  txs.reserve(sortedTxs.size());
//  for (auto hash : sortedTxs) {
//    CTransaction tx;
//    _bitcoind_getrawtransaction(bitcoind, hash, tx, 0);
//    txs.push_back(tx);
//  }
//}

void Log1Producer::syncBitcoind() {
  if (!running_) { return; }
  LogScope ls("Log1Producer::syncBitcoind()");
  //
  // 假设bitcoind在我们同步的过程中，是不会发生剧烈分叉的（剧烈分叉是指在向前追的那条链发生
  // 迁移了，导致当前追的链失效）。如果发生剧烈分叉则导致异常退出，再次启动则会首先回退再跟
  // 进，依然可以同步上去。
  //
  // 第一步，先尝试找到高度和哈希一致的块，若log1最前面的不符合，则回退直至找到一致的块
  // 第二步，从一致块高度开始，每次加一，向前追，直至与bitcoind高度一致
  //
  BitcoinRpc bitcoind(Config::GConfig.get("bitcoind.uri"));

  int32_t log0FirstLineHeight = -1;
  uint256 log0FirstLineHash;

  // 读取 log0 0.log 首行
  {
    ifstream fin(Strings::Format("%s/files/0.log", log0Dir_.c_str()));
    string line;
    Log1 log0Item;  // log0 里记录的也是log1格式
    if (!getline(fin, line)) {
      THROW_EXCEPTION_DBEX("read log0 first line failure");
    }
    log0Item.parse(line);
    if (!log0Item.isBlock()) {
      THROW_EXCEPTION_DBEX("log0 first line is NOT block");
    }
    log0FirstLineHeight = log0Item.blockHeight_;
    log0FirstLineHash   = log0Item.getBlock().GetHash();

    log0FileIndex_  = 0;
    log0FileOffset_ = fin.tellg();
  }

  //
  // 第一步，先尝试找到高度和哈希一致的块，若log1最前面的不符合，则回退直至找到一致的块
  //
  assert(chain_.size() >= 1);
  while (running_) {
    if (chain_.getCurHeight() == -1) {
      break;
    }
    // 检测最后一个块(即chain_的当前块)是否一致
    const string bitcoindHashStr = _bitcoind_getBlockHashByHeight(bitcoind, chain_.getCurHeight());
    if (chain_.getCurHash().ToString() == bitcoindHashStr) {
      LOG_INFO("found begin block, height: %d, hash: %s",
               chain_.getCurHeight(), chain_.getCurHash().ToString().c_str());
      break;
    }

    // 不一致，弹出最后一个块
    chain_.pop();
    if (chain_.size() == 0) {
      THROW_EXCEPTION_DBEX("can't find matched block, bitcoind has a big fork");
    }

    LOG_INFO("chain pop block, height: %d, hash: %s",
             chain_.getCurHeight(), chain_.getCurHash().ToString().c_str());

    // 写log1
    CBlock block;
    _bitcoind_getBlockByHash(bitcoind, chain_.getCurHash().ToString(), block);
    writeLog1Block(chain_.getCurHeight(), block);
    LOG_INFO("sync bitcoind block(-), height: %d, hash: %s",
             chain_.getCurHeight(), chain_.getCurHash().ToString().c_str());
  }

  assert(chain_.getCurHeight() <= log0FirstLineHeight);

  //
  // 第二步，从一致块高度开始，每次加一，向前追，直至与 log0FirstLineHeight 高度一致
  //
  LOG_INFO("log0 first block height: %d", log0FirstLineHeight);
  while (chain_.getCurHeight() < log0FirstLineHeight && running_) {
    const int32_t height = chain_.getCurHeight() + 1;
    const string hashStr = _bitcoind_getBlockHashByHeight(bitcoind, chain_.getCurHeight() + 1);
    LOG_INFO("sync bitcoind block(+), height: %d, hash: %s", height, hashStr.c_str());

    CBlock block;
    _bitcoind_getBlockByHash(bitcoind, hashStr, block);

    assert(block.GetHash().ToString() == hashStr);
    assert(chain_.getCurHash() == block.hashPrevBlock);

    chain_.push(height, block.GetHash(), block.hashPrevBlock);
    writeLog1Block(height, block);
  }
}

void Log1Producer::writeLog0IdxOffset() {
  const string status = Strings::Format("%d,%lld", log0FileIndex_, log0FileOffset_);
  filePutContents(log0StatusFile_, status);
}

void Log1Producer::getLog0IdxOffset() {
  string content;
  if (!fileGetContents(log0StatusFile_, content)) {
    THROW_EXCEPTION_DBEX("fileGetContents failure, file: %s", log0StatusFile_.c_str());
  }
  vector<string> arr = split(content, ',');
  assert(arr.size() == 2);

  log0FileIndex_  = atoi(arr[0].c_str());
  log0FileOffset_ = atoi64(arr[1].c_str());
}

//void Log1Producer::syncLog0() {
//  if (!running_) { return; }
//  LogScope ls("Log1Producer::syncLog0()");
//  bool syncSuccess = false;
//
//  try {
//    fs::path beginFile(Strings::Format("%s/BEGIN", log0Dir_.c_str()));
//    log0BeginFileLastModifyTime_ = fs::last_write_time(beginFile);
//  }
//  catch (boost::filesystem::filesystem_error &e)
//  {
//    THROW_EXCEPTION_DBEX("can't get log0 begin file last modify time: %s", e.what());
//  }
//
//  //
//  // 遍历 log0 所有文件，直至找到一样的块，若找到则同步完成
//  //
//  std::set<int32_t> filesIdxs;  // log0 所有文件
//  fs::path filesPath(Strings::Format("%s/files", log0Dir_.c_str()));
//  tryCreateDirectory(filesPath);
//  for (fs::directory_iterator end, it(filesPath); it != end; ++it) {
//    filesIdxs.insert(atoi(it->path().stem().c_str()));
//  }
//
//  // 反序遍历，从最新的文件开始找
//  for (auto it = filesIdxs.rbegin(); it != filesIdxs.rend(); it++) {
//    ifstream fin(Strings::Format("%s/files/%d.log", log0Dir_.c_str(), *it));
//    string line;
//    Log1 log0Item;  // log0 里记录的也是log1格式
//    while (getline(fin, line)) {
//      log0Item.parse(line);
//      if (log0Item.isTx()) { continue; }
//      assert(log0Item.isBlock());
//      if (log0Item.blockHeight_         != chain_.getCurHeight() ||
//          log0Item.getBlock().GetHash() != chain_.getCurHash()) {
//        continue;
//      }
//      // 找到高度和哈希一致的块
//      log0FileIndex_  = *it;
//      log0FileOffset_ = fin.tellg();
//      LOG_INFO("sync log0 success, file idx: %d, offset: %lld",
//               log0FileIndex_, log0FileOffset_);
//      syncSuccess = true;
//      break;
//    } /* /while */
//
//    if (syncSuccess) { break; }
//  } /* /for */
//
//  if (!syncSuccess) {
//    THROW_EXCEPTION_DBEX("sync log0 failure");
//  }
//}

void Log1Producer::tryRemoveOldLog0() {
  const int32_t keepLogNum = (int32_t)Config::GConfig.getInt("log0.files.max.num", 24 * 3);
  int32_t fileIdx = log0FileIndex_ - keepLogNum;

  // 遍历，删除所有小序列号的文件
  while (fileIdx >= 0) {
    const string file = Strings::Format("%s/files/%d.log",
                                        log0Dir_.c_str(), fileIdx--);
    if (!fs::exists(fs::path(file))) {
      break;
    }
    // try delete
    LOG_INFO("remove old log0: %s", file.c_str());
    if (!fs::remove(fs::path(file))) {
      THROW_EXCEPTION_DBEX("remove old log0 failure: %s", file.c_str());
    }
  }
}

// 尝试从 log0 中读取 N 行日志
void Log1Producer::tryReadLog0(vector<string> &lines) {
  const string currFile = Strings::Format("%s/files/%d.log",
                                          log0Dir_.c_str(), log0FileIndex_);
  const string nextFile = Strings::Format("%s/files/%d.log",
                                          log0Dir_.c_str(), log0FileIndex_ + 1);

  // 判断是否存在下一个文件，需要在读取当前文件之间判断，防止读取漏掉现有文件的最后内容
  const bool isNextExist = fs::exists(fs::path(nextFile));

  //
  // 判读 log0 是否改变. 改变后则抛出异常，log1producer 退出，重新初始化并运行
  //
  try {
    fs::path beginFile(Strings::Format("%s/BEGIN", log0Dir_.c_str()));
    if (log0BeginFileLastModifyTime_ != fs::last_write_time(beginFile)) {
      THROW_EXCEPTION_DBEX("log0 begin file has been changed, curr: %u, old: %u",
                           (uint32_t)fs::last_write_time(beginFile),
                           (uint32_t)log0BeginFileLastModifyTime_);
    }
  }
  catch (boost::filesystem::filesystem_error &e)
  {
    THROW_EXCEPTION_DBEX("can't get log0 begin file last modify time: %s", e.what());
  }

  //
  // 打开文件并尝试读取新行
  //
  ifstream log0Ifstream(currFile);
  if (!log0Ifstream.is_open()) {
    THROW_EXCEPTION_DBEX("open file failure: %s", currFile.c_str());
  }
  // check file size
  log0Ifstream.seekg(0, log0Ifstream.end);
  long length = log0Ifstream.tellg();
  if (length < log0FileOffset_) {
    THROW_EXCEPTION_DBEX("file has been changed: %s", currFile.c_str());
  }
  // seek to end from begin
  log0Ifstream.seekg(log0FileOffset_, log0Ifstream.beg);
  string line;
  while (getline(log0Ifstream, line)) {  // getline()读不到内容，则会关闭 ifstream
    if (log0Ifstream.eof()) {
      // eof 表示没有遇到 \n 就抵达文件尾部了，通常意味着未完全读取一行
      // 读取完最后一行后，再读取一次，才会导致 eof() 为 true
      break;
    }
    lines.push_back(line);
    log0FileOffset_ = log0Ifstream.tellg();

    if (lines.size() > 500) {  // 每次最多处理500条日志
      LOG_WARN("reach max limit, stop load log0 items");
      break;
    }
  }
  if (lines.size() > 0) {
    return;
  }

  //
  // 探测新文件，仅当前面没有读取到新内容的时候
  //
  if (isNextExist == true && lines.size() == 0) {
    // 存在新的文件，切换索引，重置offset
    log0FileIndex_++;
    log0FileOffset_ = 0;
    LOG_INFO("swith log0 file, old: %s, new: %s ", currFile.c_str(), nextFile.c_str());

    tryRemoveOldLog0();
  }
}

void Log1Producer::run() {
  LogScope ls("Log1Producer::run()");

  while (running_) {
    vector<string> lines;
    tryReadLog0(lines);

    if (!running_) { break; }
    if (lines.size() == 0) {
      UniqueLock ul(lock_);
      // 默认等待N毫秒，直至超时，中间有人触发，则立即continue读取记录
      changed_.wait_for(ul, chrono::milliseconds(3*1000));
      continue;
    }

    for (const auto &line : lines) {
      Log1 log0Item;
      log0Item.parse(line);
      //
      // Tx
      //
      if (log0Item.isAcceptTx() || log0Item.isRemoveTx()) {
        // 交易的容错是最强的，即使当前块已经错乱了，推入交易仍然不会出问题
        writeLog1Tx(log0Item.type_, log0Item.getTx());
      }
      //
      // Block
      //
      else if (log0Item.isBlock()) {
        // 推入当前链中，如果块的前后关联有问题，则会抛出异常中断程序
        chain_.push(log0Item.blockHeight_,
                    log0Item.getBlock().GetHash(), log0Item.getBlock().hashPrevBlock);
        writeLog1Block(log0Item.blockHeight_, log0Item.getBlock());
        LOG_INFO("chain push block, height: %d, hash: %s", log0Item.blockHeight_,
                 log0Item.getBlock().GetHash().ToString().c_str());
      } else {
        THROW_EXCEPTION_DBEX("invalid log0 type, log line: %s", line.c_str());
      }
    } /* /for */

    // 更新最后读取的文件 index & offset
    // 这里假定处理 lines 的过程中，程序是不会被 'kill -9' 的，正常的'ctrl + c'是可以处理的
    writeLog0IdxOffset();

  } /* /while */
}

void Log1Producer::doNotifyLog2Producer() {
  //
  // 只读打开后就关闭掉，会产生一个通知事件，由 log2producer 捕获
  //     IN_CLOSE_NOWRITE: 一个以只读方式打开的文件或目录被关闭。
  //
  FILE *f = fopen(notifyFileLog2Producer_.c_str(), "r");
  assert(f != nullptr);
  fclose(f);
}

void Log1Producer::writeLog1(const int32_t type, const string &line) {
  //
  // 写 log1 日志
  //
  if (log1FileIndex_ == -1) {
    log1FileIndex_ = 0; // reset to zero as begin index
  }
  const string file = Strings::Format("%s/files/%d.log", log1Dir_.c_str(), log1FileIndex_);
  if (log1FileHandler_ == nullptr) {
    log1FileHandler_ = fopen(file.c_str(), "a");  // append mode
  }
  if (log1FileHandler_ == nullptr) {
    THROW_EXCEPTION_DBEX("open file failure: %s", file.c_str());
  }
  const string logLine = Strings::Format("%s,%d,%s\n",
                                         date("%F %T").c_str(),
                                         type, line.c_str());
  size_t res = fwrite(logLine.c_str(), 1U, logLine.length(), log1FileHandler_);
  if (res != logLine.length()) {
    THROW_EXCEPTION_DBEX("fwrite return size_t(%llu) is NOT match line length: %llu, file: %s",
                         res, logLine.length(), file.c_str());
  }
  fflush(log1FileHandler_);  // fwrite 后执行 fflush 保证其他程序立即可以读取到

  // 写完成后，再执行通知
  // 如果 log2producer 直接监听日志文件的 IN_MODIFY 时间，可能读取不到完整的一行，未写完
  // 就触发了 IN_MODIFY 时间，所以用单独文件去触发通知
  doNotifyLog2Producer();

  //
  // 切换 log1 日志：超过最大文件长度则关闭文件，下次写入时会自动打开新的文件
  //
  int64_t log1FileMaxSize = Config::GConfig.getInt("log1.file.max.size.mb",
                                                   50) * 1024 * 1024;
  if (log1FileMaxSize > std::numeric_limits<int32_t>::max()) {
    log1FileMaxSize = std::numeric_limits<int32_t>::max();
    LOG_WARN("log1.file.max.size is too large, reset to: %lld", log1FileMaxSize);
  }
  long position = ftell(log1FileHandler_);
  if (position == -1L) {
    THROW_EXCEPTION_DBEX("ftell failure: %s", file.c_str());
  }
  if (position > log1FileMaxSize) {
    fsync(fileno(log1FileHandler_));
    fclose(log1FileHandler_);
    log1FileHandler_ = nullptr;
    log1FileIndex_++;
    LOG_INFO("log1's size(%lld) reach max(%lld), switch to new file index: %d",
             position, log1FileMaxSize, log1FileIndex_);
  }
}

void Log1Producer::writeLog1Tx(const int32_t type, const CTransaction &tx) {
  const string hex = EncodeHexTx(tx);
  const string hashStr = tx.GetHash().ToString();
  writeLog1(type,
            Strings::Format("%s|%s", hashStr.c_str(), hex.c_str()));

  LOG_INFO("write log1 tx: %s", hashStr.c_str());
}

void Log1Producer::writeLog1Block(const int32_t height, const CBlock &block) {
  CDataStream ssBlock(SER_NETWORK, BITCOIN_PROTOCOL_VERSION);
  ssBlock << block;
  std::string strHex = HexStr(ssBlock.begin(), ssBlock.end());
  const string hashStr = block.GetHash().ToString();
  writeLog1(Log1::TYPE_BLOCK,
            Strings::Format("%d|%s|%s", height, hashStr.c_str(), strHex.c_str()));

  LOG_INFO("write log1 block(%d): %s", height, hashStr.c_str());
}

