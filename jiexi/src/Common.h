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

/**
 * Common header files, most common used util classes
 * include string, time, thread, log and various exceptions
 */

#ifndef COMMON_H_
#define COMMON_H_

#include "bitcoin/uint256.h"
#include "bitcoin/key.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <time.h>

#include <stdexcept>
#include <string>
#include <vector>
#include <deque>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <random>
#include <memory>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>

#include <boost/filesystem.hpp>

using std::string;
using std::vector;
using std::deque;
using std::map;
using std::set;
using std::pair;
using std::make_pair;
using std::unique_ptr;
using std::shared_ptr;
using std::make_shared;
using std::atomic;
using std::thread;
using std::mutex;
using std::lock_guard;
using std::unique_lock;
using std::condition_variable;

typedef int8_t int8;
typedef uint8_t uint8;
typedef int16_t int16;
typedef uint16_t uint16;
typedef int32_t int32;
typedef uint32_t uint32;
typedef int64_t int64;
typedef uint64_t uint64;
typedef lock_guard<mutex> ScopeLock;
typedef unique_lock<mutex> UniqueLock;
typedef condition_variable Condition;

/**
 * byte order conversion utils
 */
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
inline uint16 HToBe(uint16 v) {
  return (v >> 8) | (v << 8);
}
inline uint32 HToBe(uint32 v) {
  return ((v & 0xff000000) >> 24) |
  ((v & 0x00ff0000) >> 8) |
  ((v & 0x0000ff00) << 8) |
  ((v & 0x000000ff) << 24);
}
inline uint64 HToBe(uint64 v) {
  return ((v & 0xff00000000000000ULL) >> 56) |
  ((v & 0x00ff000000000000ULL) >> 40) |
  ((v & 0x0000ff0000000000ULL) >> 24) |
  ((v & 0x000000ff00000000ULL) >>  8) |
  ((v & 0x00000000ff000000ULL) <<  8) |
  ((v & 0x0000000000ff0000ULL) << 24) |
  ((v & 0x000000000000ff00ULL) << 40) |
  ((v & 0x00000000000000ffULL) << 56);
}
#else
inline uint16 HToBe(uint16 v) {
  return v;
}
inline uint32 HToBe(uint32 v) {
  return v;
}
inline uint64 HToBe(uint64 v) {
  return v;
}
#endif
inline int16 HToBe(int16 v) {
  return (int16)HToBe((uint16)v);
}
inline int32 HToBe(int32 v) {
  return (int32)HToBe((uint32)v);
}
inline int64 HToBe(int64 v) {
  return (int64)HToBe((uint64)v);
}

bool Hex2Bin(const char *in, size_t size, vector<char> &out);
bool Hex2Bin(const char *in, vector<char> &out);
void Bin2Hex(const uint8 *in, size_t len, string &str);
void Bin2Hex(const vector<char> &in, string &str);
void Bin2HexR(const vector<char> &in, string &str);

bool IsDebug();

string Ip2Str(uint32 ip);
inline string Ip2StrNetOrder(in_addr_t ip) {
  return Ip2Str(ntohl(*(uint32_t *)&ip));
}
uint32 Str2Ip(const char *s);
uint32 GetLocalPrimaryIpInt();
bool getMacAddress(char *macAddr, const char *if_name);
bool GetLocalPrimaryMacAddress(string &primaryMac, const uint32 primaryIp);

inline void BitsToDifficulty(uint32 bits, double &difficulty) {
  int nShift = (bits >> 24) & 0xff;
  double dDiff = (double)0x0000ffff / (double)(bits & 0x00ffffff);
  while (nShift < 29) {
    dDiff *= 256.0;
    nShift++;
  }
  while (nShift > 29) {
    dDiff /= 256.0;
    nShift--;
  }
  difficulty = dDiff;
}

inline void BitsToDifficulty(uint32 bits, uint64 &difficulty) {
  double diff;
  BitsToDifficulty(bits, diff);
  difficulty = (uint64)diff;
}

void BitsToTarget(uint32 bits, uint256 & target);
uint64 TargetToBdiff(uint256 &target);
uint64 TargetToBdiff(const string &str);
uint64 TargetToPdiff(uint256 &target);
uint64 TargetToPdiff(const string &str);
// only support perfect diff, like 2^i, 3*(2^i), 5*(2^i)
uint32 DiffToBits(uint64 diff);
void DiffToTarget(uint64 diff, uint256 & target);

bool SignMessage(const CKey &key, const string &strMessage, string &signature);
bool VerifyMessage(const string &strAddress, const string &strSign,
                   const string &strMessage);

uint64_t hash_16_bytes(uint64_t low, uint64_t high);

string humanFormat(double size, int unitSize);

inline string humanFormat1000(double size) {
  return humanFormat(size, 1000);
}
inline string humanFormat1024(double size) {
  return humanFormat(size, 1024);
}

bool curlCallUrl(const string &url);

void writePid2FileOrExit(const char *filename);
void writeTime2File(const char *filename, uint64 t);
void killSelf();

void runCommand(const std::string &strCommand);

string date(const char *format, const time_t timestamp);
inline string date(const char *format) {
  return date(format, time(nullptr));
}
time_t str2time(const char *str, const char *format);
inline time_t str2time(const char *str) {
  return str2time(str, "%F %T");
}

inline void sleepMs(const uint32_t ms) {
  if (ms <= 1000) {
    usleep(ms * 1000);
  } else {
    sleep(ms/1000);
    usleep((ms % 1000) * 1000);
  }
}

string score2Str(double s);

inline bool isLanIP(uint32 ip) {
  ip = htonl(ip);
  const uint8 *p = (const uint8 *)&ip;
  // A: 10.0.0.0--10.255.255.255
  if (*p == 10) {
    return true;
  }
  // B: 172.16.0.0--172.31.255.255
  if (*p == 172 && *(p+1) >= 16 && *(p+1) <= 31) {
    return true;
  }
  // C: 192.168.0.0--192.168.255.255
  if (*p == 192 && *(p+1) == 168) {
    return true;
  }
  return false;
}

bool tryCreateDirectory(const boost::filesystem::path& p);

/**
 * String conversion utils
 */
class Strings {
public:
  static string ToString(int32 v);
  static string ToString(uint32 v);
  static string ToString(int64 v);
  static string ToString(int64 v, char pad, int64 len);
  static string ToString(uint64 v);
  static string ToString(bool v);
  static string ToString(float v);
  static string ToString(double v);
  static string ToString(const vector<string> & vs);
  
  static int64 toInt(const string & str);
  
  static bool toBool(const string & str);
  
  static float  toFloat(const string & str);

  static double toDouble(const string & str);
  
  static string Format(const char * fmt, ...);
  
  static void Append(string & dest, const char * fmt, ...);
  
  static string ToUpper(const string & name);
  
  static string ToLower(const string & name);
  
  static string Trim(const string & str);
  
  static void Split(const string & src, const string & sep,
                    vector<string> & dest, bool clean = false);
  
  static string Join(const vector<string> & strs,
                     const string & sep);
  
  static string ReplaceAll(const string & src, const string & find,
                           const string & replace);
  
  static bool StartsWith(const string & str, const string & prefix);
  
  static bool EndsWith(const string & str, const string & suffix);
  
  static int64 LastSeparator(const string & str, char sep);
  
  static string LastPart(const string & str, char sep);
  
  static const char * LastPartStart(const string & str, char sep);
  
  static string toBase64(const string & str);
  
  static string fromBase64(const string & str);
  
  static string addslashes(const char *pStr, const char *match);
  static string escape(const char *pStr);
  static string escape(const string &inStr);
  
  static void reverse(string &inStr);
};

/**
 * time primitives
 */
class Time {
public:
  static const int64 MAX = 0x7fffffffffffffffL;
  // monotonic timer in ns
  static int64 CurrentMonoNano();
  // monotonic timer in ms
  static int64 CurrentMonoMill() {
    return CurrentMonoNano() / 1000000;
  }
  // ns since epoch
  static int64 CurrentTimeNano();
  // ms since epoch
  static int64 CurrentTimeMill() {
    return CurrentTimeNano() / 1000000;
  }
  static int64 Timeout(int64 timeout) {
    if (timeout > 0) {
      int64 ret = Time::CurrentMonoMill() + timeout;
      return ret > 0 ? ret : MAX;
    } else {
      return MAX;
    }
  }
};

/**
 * thread utils
 */
class Thread {
public:
  static uint64 CurrentId();
  static void Sleep(uint32 millisec);
  static void AddStackTrace(string & dst);
  static void LogStackTrace(const char * msg);
};

/**
 * log utils
 */
enum LogLevel {
  LOG_LEVEL_DEBUG = 0,
  LOG_LEVEL_INFO  = 1,
  LOG_LEVEL_WARN  = 2,
  LOG_LEVEL_ERROR = 3,
  LOG_LEVEL_FATAL = 4
};

class Log {
protected:
  static LogLevel Level;
  static FILE * Device;
  static mutex LogMutex;
public:
  static LogLevel GetLevel() {
    return Level;
  }
  static void SetLevel(LogLevel level) {
    Level = level;
  }
  static FILE * GetDevice() {
    return Device;
  }
  static void SetDevice(FILE * device) {
    Device = device;
  }
  static void LogMessage(LogLevel level, const char * fmt, ...);
};


#define LOG_EX(level, fmt, args...) \
if (Log::GetDevice() &&\
level >= Log::GetLevel()) {\
Log::LogMessage(level, fmt, ##args);\
}
#define LOG_DEBUG(fmt, args...) LOG_EX(LOG_LEVEL_DEBUG, fmt, ##args)
#define LOG_INFO(fmt, args...) LOG_EX(LOG_LEVEL_INFO, fmt, ##args)
#define LOG_WARN(fmt, args...) LOG_EX(LOG_LEVEL_WARN, fmt, ##args)
#define LOG_ERROR(fmt, args...) LOG_EX(LOG_LEVEL_ERROR, fmt, ##args)
#define LOG_FATAL(fmt, args...) LOG_EX(LOG_LEVEL_FATAL, fmt, ##args)
#define LOG(fmt, args...) LOG_INFO(fmt, ##args)

class LogScope {
private:
  const char * name;
public:
  LogScope(const char * name) : name(name) {
    LOG("[%s] started", name);
  }
  ~LogScope() {
    LOG("[%s] finished", name);
  }
};


/**
 * Random utils
 * use lock for multi-thread synchronization
 */
class Random {
public:
  static uint32 NextUInt32();
  static uint64 NextUInt64();
  static float NextFloat();
  static double NextDouble();
  static void NextBytes(uint32 len, string & bytes);
  static void NextVisableBytes(uint32 len, string & text);
  static void NextUUID(string & uuid);
};

#define ERROR_UNKNOWN 127

class Exception: public std::exception {
public:
  // type enum to string
  static const char * TypeToString(int type);
  
protected:
  int _type;
  string _reason;
  string _stackTrace;
  
public:
  Exception(int type, const char * where, const string & what);
  Exception(const char * where, const string & what);
  Exception(const string & what);
  virtual ~Exception() throw () {
  }
  
  virtual const char* what() const throw () {
    return _reason.c_str();
  }
  
  const int type() {
    return _type;
  }
  
  string & reason() {
    return _reason;
  }
  
  string & stackTrace() {
    return _stackTrace;
  }
  
  void logWarn();
  
  void logInfo();
  
  static void SetLogException(bool logException);
};

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define AT __FILE__ ":" TOSTRING(__LINE__)
#define THROW_EXCEPTION(type, what) throw Exception(type, (AT), (what))
#define THROW_EXCEPTION_EX(type, fmt, args...) \
throw Exception(type, (AT), Strings::Format(fmt, ##args))

#define THROW_EXCEPTION_DB(what) throw Exception(EIO, (AT), (what))
#define THROW_EXCEPTION_DBEX(fmt, args...) \
throw Exception(EIO, (AT), Strings::Format(fmt, ##args))

/**
 * Config
 * after load(calling parseConfig and parseArgs), it is read only
 * so used in multi-thread without lock should be fine
 */
class Config {
protected:
  std::unordered_map<string, string> kvs_;

public:
  Config();
  ~Config();
  
  void parseConfig(const char * filename, bool required=true);
  
  void parseArgs(int argc, char ** argv);
  
  string get(const string & key);
  string get(const string & key, const string & defaultValue);
  
  bool getBool(const string & key);
  bool getBool(const string & key, bool defaultValue);
  
  int64 getInt(const string & key);
  int64 getInt(const string & key, int64 defaultValue);

  double getDouble(const string & key);
  double getDouble(const string & key, double defaultValue);
  
  static Config GConfig;
};


#endif /* COMMON_H_ */
