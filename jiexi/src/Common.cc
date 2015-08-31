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
#include <arpa/inet.h>
#include <cxxabi.h>
#include <errno.h>
#include <execinfo.h>
#include <time.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <net/if.h>
#include <pthread.h>
#include <sys/time.h>
#include <stdarg.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <zlib.h>

#include <algorithm>
#include <regex>

#include <curl/curl.h>
#include <boost/thread.hpp>

#ifdef __MACH__
#include <mach/clock.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#endif
#include "Common.h"

#include "bitcoin/uint256.h"
#include "bitcoin/base58.h"
#include "bitcoin/core.h"
#include "bitcoin/util.h"


static const char _hexchars[] = "0123456789abcdef";

static inline int _hex2bin_char(const char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return (c - 'a') + 10;
  if (c >= 'A' && c <= 'F')
    return (c - 'A') + 10;
  return -1;
}

bool Hex2Bin(const char *in, size_t size, vector<char> &out) {
  out.clear();
  out.reserve(size/2);

  uint8 h, l;
  // skip space, 0x
  const char *psz = in;
  while (isspace(*psz))
    psz++;
  if (psz[0] == '0' && tolower(psz[1]) == 'x')
    psz += 2;

  // convert
  while (psz + 2 <= (char *)in + size) {
    h = _hex2bin_char(*psz++);
    l = _hex2bin_char(*psz++);
    out.push_back((h << 4) | l);
  }
  return true;

}

bool Hex2Bin(const char *in, vector<char> &out) {
  out.clear();
  out.reserve(strlen(in)/2);

  uint8 h, l;
  // skip space, 0x
  const char *psz = in;
  while (isspace(*psz))
    psz++;
  if (psz[0] == '0' && tolower(psz[1]) == 'x')
    psz += 2;
  
  if (strlen(psz) % 2 == 1) { return false; }
  
  // convert
  while (*psz != '\0' && *(psz + 1) != '\0') {
    h = _hex2bin_char(*psz++);
    l = _hex2bin_char(*psz++);
    out.push_back((h << 4) | l);
  }
  return true;
}

void Bin2Hex(const uint8 *in, size_t len, string &str) {
  str.clear();
  const uint8 *p = in;
  while (len--) {
    str.push_back(_hexchars[p[0] >> 4]);
    str.push_back(_hexchars[p[0] & 0xf]);
    ++p;
  }
}

void Bin2Hex(const vector<char> &in, string &str) {
  Bin2Hex((uint8 *)in.data(), in.size(), str);
}

void Bin2HexR(const vector<char> &in, string &str) {
  vector<char> r;
  for (auto it = in.rbegin(); it != in.rend(); ++it) {
    r.push_back(*it);
  }
  Bin2Hex(r, str);
}

static int __isDebug = -1;
bool IsDebug() {
  if (__isDebug == -1) {
    __isDebug = Config::GConfig.getBool("debug", true) ? 1 : 0;
  }
  return __isDebug == 1 ? true : false;
}

string Ip2Str(uint32 ip) {
  char buf[INET_ADDRSTRLEN];
  ip = htonl(ip);
  inet_ntop(AF_INET, &(ip), buf, INET_ADDRSTRLEN);
  return string(buf);
}

uint32 Str2Ip(const char *s) {
  uint32 ipInt;
  inet_pton(AF_INET, s, &ipInt);
  return ntohl(ipInt);
}

uint32 GetLocalPrimaryIpInt() {
	int sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock == -1) { return 0; }
	
	const char* kGoogleDnsIp = "8.8.8.8";
	uint16_t kDnsPort = 53;
	struct sockaddr_in serv;
	memset(&serv, 0, sizeof(serv));
	serv.sin_family = AF_INET;
	serv.sin_addr.s_addr = inet_addr(kGoogleDnsIp);
	serv.sin_port = htons(kDnsPort);
	
	int err = connect(sock, (const sockaddr*) &serv, sizeof(serv));
	if (err == -1) { return 0; }
	
	sockaddr_in name;
	socklen_t namelen = sizeof(name);
	err = getsockname(sock, (sockaddr*) &name, &namelen);
	assert(err != -1);
	close(sock);
	
	return *(uint32 *)&name.sin_addr;
}

#ifdef SIOCGIFHWADDR
bool getMacAddress(char *macAddr, const char *if_name) {
  struct ifreq ifinfo;
  strcpy(ifinfo.ifr_name, if_name);
  int sd = socket(AF_INET, SOCK_DGRAM, 0);
  int result = ioctl(sd, SIOCGIFHWADDR, &ifinfo);
  close(sd);
  
  if ((result == 0) && (ifinfo.ifr_hwaddr.sa_family == 1)) {
    memcpy(macAddr, ifinfo.ifr_hwaddr.sa_data, IFHWADDRLEN);
    return true;
  }
  else {
    return false;
  }
}
#else
#include <net/if_dl.h>
bool getMacAddress(char *macAddr, const char *if_name) {
  ifaddrs* iflist;
  bool found = false;
  if (getifaddrs(&iflist) != 0) {
    return found;
  }
  for (ifaddrs* cur = iflist; cur; cur = cur->ifa_next) {
    if ((cur->ifa_addr->sa_family == AF_LINK) &&
        (strcmp(cur->ifa_name, if_name) == 0) &&
        cur->ifa_addr) {
      sockaddr_dl* sdl = (sockaddr_dl*)cur->ifa_addr;
      memcpy(macAddr, LLADDR(sdl), sdl->sdl_alen);
      found = true;
      break;
    }
  }
  freeifaddrs(iflist);
  return found;
}
#endif


//
// http://stackoverflow.com/questions/212528/get-the-ip-address-of-the-machine
//
bool GetLocalPrimaryMacAddress(string &primaryMac, const uint32 primaryIp) {
  struct ifaddrs *ifAddrStruct = NULL;
  struct ifaddrs *ifa = NULL;
  void *tmpAddrPtr = NULL;
  getifaddrs(&ifAddrStruct);
  primaryMac.clear();
  
  for (ifa = ifAddrStruct; ifa != NULL; ifa = ifa->ifa_next) {
    // check if it is IPv4
    if (ifa->ifa_addr == NULL || ifa ->ifa_addr->sa_family != AF_INET) {
      continue;
    }
    tmpAddrPtr = &((struct sockaddr_in *)ifa->ifa_addr)->sin_addr;
    // check ip
    if (primaryIp == *(uint32 *)tmpAddrPtr) {
      char buf[6];
      getMacAddress(buf, ifa->ifa_name);
      Bin2Hex((uint8 *)buf, sizeof(buf), primaryMac);
      break;
    }
  }
  if (ifAddrStruct != NULL) {
    freeifaddrs(ifAddrStruct);
  }
  return primaryMac.length() > 0 ? true : false;
}



uint64 TargetToBdiff(uint256 &target) {
  CBigNum m, t;
  m.SetHex("0x00000000FFFF0000000000000000000000000000000000000000000000000000");
  t.setuint256(target);
  return strtoull((m / t).ToString().c_str(), NULL, 10);
}

uint64 TargetToBdiff(const string &str) {
  CBigNum m, t;
  m.SetHex("0x00000000FFFF0000000000000000000000000000000000000000000000000000");
  t.SetHex(str);
  return strtoull((m / t).ToString().c_str(), NULL, 10);
}

uint64 TargetToPdiff(uint256 &target) {
  CBigNum m, t;
  m.SetHex("0x00000000FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF");
  t.setuint256(target);
  return strtoull((m / t).ToString().c_str(), NULL, 10);
}

uint64 TargetToPdiff(const string &str) {
  CBigNum m, t;
  m.SetHex("0x00000000FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF");
  t.SetHex(str);
  return strtoull((m / t).ToString().c_str(), NULL, 10);
}


static const uint32 BITS_DIFF1 = 0x1d00ffff;

void BitsToTarget(uint32 bits, uint256 & target) {
  uint64 nbytes = (bits >> 24) & 0xff;
  target = bits & 0xffffffULL;
  target <<= (8 * ((uint8)nbytes - 3));
}

uint32 DiffToBits(uint64 diff) {
  uint64 origdiff = diff;
  uint64 nbytes = (BITS_DIFF1 >> 24) & 0xff;
  uint64 value = BITS_DIFF1 & 0xffffffULL;

  if (diff == 0) {
    LOG_FATAL("[DiffToBits] diff is zero");
    THROW_EXCEPTION_EX(EINVAL, "diff is zero");
  }

  while (diff % 256 == 0) {
    nbytes -= 1;
    diff /= 256;
  }
  if (value % diff == 0) {
    value /= diff;
  } else if ((value << 8) % diff == 0) {
    nbytes -= 1;
    value <<= 8;
    value /= diff;
  } else {
    THROW_EXCEPTION_EX(EINVAL, "diff(%llu) not perfect, can't convert to bits",
                       origdiff);
  }
  if (value > 0x00ffffffULL) {
    // overflow... should not happen
    THROW_EXCEPTION_EX(EOVERFLOW, "diff overflow, code bug");
  }
  return (uint32)(value | (nbytes << 24));
}

void DiffToTarget(uint64 diff, uint256 & target) {
  BitsToTarget(DiffToBits(diff), target);
}

//
// bitcoin verify message
//
const string strMessageMagic = "Bitcoin Signed Message:\n";
bool VerifyMessage(const string &strAddress, const string &strSign,
                   const string &strMessage) {
  CBitcoinAddress addr(strAddress);
  if (!addr.IsValid()) {
    return false;
  }
  CKeyID keyID;
  if (!addr.GetKeyID(keyID)) {
    return false;
  }
  bool fInvalid = false;
  vector<unsigned char> vchSig = DecodeBase64(strSign.c_str(), &fInvalid);
  if (fInvalid) {
    return false;
  }
  CHashWriter ss(SER_GETHASH, 0);
  ss << strMessageMagic;
  ss << strMessage;
  
  CPubKey pubkey;
  if (!pubkey.RecoverCompact(ss.GetHash(), vchSig)) {
    return false;
  }
  return (pubkey.GetID() == keyID);
}


bool SignMessage(const CKey &key, const string &strMessage, string &signature) {
  CHashWriter ss(SER_GETHASH, 0);
  ss << strMessageMagic;
  ss << strMessage;
  
  vector<unsigned char> vchSig;
  if (!key.SignCompact(ss.GetHash(), vchSig))
    return false;
  
  signature = EncodeBase64(&vchSig[0], vchSig.size());
  return true;
}

//
// hash function, see:
// http://root.cern.ch/svn/root/vendors/llvm/include/llvm/ADT/Hashing.h
// http://hackage.haskell.org/package/cityhash-0.0.2/src/cbits/city.h
//
uint64_t hash_16_bytes(uint64_t low, uint64_t high) {
  // Murmur-inspired hashing.
  const uint64_t kMul = 0x9ddfea08eb382d69ULL;
  uint64_t a = (low ^ high) * kMul;
  a ^= (a >> 47);
  uint64_t b = (high ^ a) * kMul;
  b ^= (b >> 47);
  b *= kMul;
  return b;
}

string humanFormat(double size, int unitSize) {
  char buf[16];
  int i = 0;
  const char* units[] = {"", "k", "M", "G", "T", "P", "E", "Z", "Y"};
  while (size > unitSize) {
    size /= unitSize;
    i++;
  }
  sprintf(buf, "%.*f %s", i, size, units[i]);
  return string(buf);
}

static size_t _curl_write_data_empty(void *buffer, size_t size,
                                     size_t nmemb, void *userp) {
  return size * nmemb;
}

bool curlCallUrl(const string &url) {
  CURL *curl = nullptr;
  CURLcode res;

  curl = curl_easy_init();
  if (!curl) {
    LOG_FATAL("curl_easy_init() failed");
    return false;
  }

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 3);         // max time the request is allowed
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 1);  // timeout for the connect phase
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 3L); // allow redirection
  // do not output to stdout
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, _curl_write_data_empty);

  /* Perform the request, res will get the return code */
  res = curl_easy_perform(curl);
  /* Check for errors */
  if(res != CURLE_OK) {
    LOG_ERROR("curl_easy_perform() failed: %s, url: %s",
              curl_easy_strerror(res), url.c_str());
  }
  curl_easy_cleanup(curl); /* always cleanup */

  //
  // 无需检查返回值，仅需要判断响应状态
  //
  if(res == CURLE_OK) {
    return true;
  }
  return false;
}

void writePid2FileOrExit(const char *filename) {
  FILE *fp = fopen(filename, "w");
  if (!fp) {
    fprintf(stderr, "can't write file: %s\n", filename);
    exit(0);
  }
  fprintf(fp, "%d", getpid());
  fclose(fp);
}

void writeTime2File(const char *filename, uint64 t) {
  FILE *fp = fopen(filename, "w");
  if (!fp) { return; }
  fprintf(fp, "%llu", (uint64)t);
  fclose(fp);
}

void killSelf() {
  kill(getpid(), SIGINT);
}

void runCommand(const std::string &strCommand) {
  LOG_INFO("runCommond: %s", strCommand.c_str());
  
  int nErr = ::system(strCommand.c_str());
  if (nErr)
    LOG_ERROR("runCommand error: system(%s) returned %d\n", strCommand.c_str(), nErr);
}

//
// %y	Year, last two digits (00-99)	01
// %Y	Year	2001
// %m	Month as a decimal number (01-12)	08
// %d	Day of the month, zero-padded (01-31)	23
// %H	Hour in 24h format (00-23)	14
// %I	Hour in 12h format (01-12)	02
// %M	Minute (00-59)	55
// %S	Second (00-61)	02
//
// %D	Short MM/DD/YY date, equivalent to %m/%d/%y	08/23/01
// %F	Short YYYY-MM-DD date, equivalent to %Y-%m-%d	2001-08-23
// %T	ISO 8601 time format (HH:MM:SS), equivalent to %H:%M:%S	14:55:02
//
string date(const char *format, const time_t timestamp) {
  char buffer[80] = {0};
  struct tm tm;
  time_t ts = timestamp;
  gmtime_r(&ts, &tm);
  strftime(buffer, sizeof(buffer), format, &tm);
  return string(buffer);
}

time_t str2time(const char *str, const char *format) {
  struct tm tm;
  strptime(str, format, &tm);
  return timegm(&tm);
}

string score2Str(double s) {
  if (s <= 0.0) {
    return "0";
  }

  // double双精度完全保证的有效数字是15位，16位只是部分数值有保证
  // s不得超过10e15
  string f;
  // 下面调整输出的有效位数，防止不够
  int p = -1;
  if (s >= 1.0) {
    p = 15 - (int)ceil(log10(s)) - 1;
  } else {
    p = 15 + -1 * (int)floor(log10(s)) - 1;
  }
  assert(p >= 0);
  // 小数部分最多25位，小于
  f = Strings::Format("%%.%df", p > 25 ? 25 : p);
  return Strings::Format(f.c_str(), s);
}


////////////////////////////////  Strings  ////////////////////////////////////

string Strings::ToString(int32 v) {
  char tmp[32];
  snprintf(tmp, 32, "%d", v);
  return tmp;
}

string Strings::ToString(uint32 v) {
  char tmp[32];
  snprintf(tmp, 32, "%u", v);
  return tmp;
}

string Strings::ToString(int64 v) {
  char tmp[32];
  snprintf(tmp, 32, "%lld", (long long)v);
  return tmp;
}

string Strings::ToString(int64 v, char pad, int64 len) {
  char tmp[32];
  snprintf(tmp, 32, "%%%c%lldlld", pad, (long long)len);
  return Format(tmp, v);
}

string Strings::ToString(uint64 v) {
  char tmp[32];
  snprintf(tmp, 32, "%llu", (unsigned long long)v);
  return tmp;
}

string Strings::ToString(bool v) {
  if (v) {
    return "true";
  } else {
    return "false";
  }
}

string Strings::ToString(float v) {
  char tmp[32];
  snprintf(tmp, 32, "%f", v);
  return tmp;
}

string Strings::ToString(double v) {
  char tmp[32];
  snprintf(tmp, 32, "%lf", v);
  return tmp;
}

string Strings::ToString(const vector<string> & vs) {
  string ret;
  ret.append("[");
  ret.append(Join(vs, ","));
  ret.append("]");
  return ret;
}

bool Strings::toBool(const string & str) {
  if (str == "true") {
    return true;
  } else {
    return false;
  }
}

int64 Strings::toInt(const string & str) {
  if (str.length() == 0) {
    return 0;
  }
  return strtoll(str.c_str(), nullptr, 10);
}

float Strings::toFloat(const string & str) {
  if (str.length() == 0) {
    return 0.0f;
  }
  return strtof(str.c_str(), nullptr);
}

double Strings::toDouble(const string & str) {
  if (str.length() == 0) {
    return 0.0;
  }
  return strtod(str.c_str(), nullptr);
}


string Strings::Format(const char * fmt, ...) {
  char tmp[512];
  string dest;
  va_list al;
  va_start(al, fmt);
  int len = vsnprintf(tmp, 512, fmt, al);
  va_end(al);
  if (len>511) {
    char * destbuff = new char[len+1];
    va_start(al, fmt);
    len = vsnprintf(destbuff, len+1, fmt, al);
    va_end(al);
    dest.append(destbuff, len);
    delete destbuff;
  } else {
    dest.append(tmp, len);
  }
  return dest;
}

void Strings::Append(string & dest, const char * fmt, ...) {
  char tmp[512];
  va_list al;
  va_start(al, fmt);
  int len = vsnprintf(tmp, 512, fmt, al);
  va_end(al);
  if (len>511) {
    char * destbuff = new char[len+1];
    va_start(al, fmt);
    len = vsnprintf(destbuff, len+1, fmt, al);
    va_end(al);
    dest.append(destbuff, len);
  } else {
    dest.append(tmp, len);
  }
}

string Strings::ToUpper(const string & name) {
  string ret = name;
  for (size_t i = 0 ; i < ret.length() ; i++) {
    ret.at(i) = ::toupper(ret[i]);
  }
  return ret;
}

string Strings::ToLower(const string & name) {
  string ret = name;
  for (size_t i = 0 ; i < ret.length() ; i++) {
    ret.at(i) = ::tolower(ret[i]);
  }
  return ret;
}

string Strings::Trim(const string & str) {
  if (str.length()==0) {
    return str;
  }
  size_t l = 0;
  while (l<str.length() && isspace(str[l])) {
    l++;
  }
  if (l>=str.length()) {
    return string();
  }
  size_t r = str.length();
  while (isspace(str[r-1])) {
    r--;
  }
  return str.substr(l, r-l);
}


void Strings::Split(const string & src, const string & sep,
                    vector<string> & dest, bool clean) {
  if (sep.length()==0) {
    return;
  }
  size_t cur = 0;
  while (true) {
    size_t pos;
    if (sep.length()==1) {
      pos = src.find(sep[0], cur);
    } else {
      pos = src.find(sep, cur);
    }
    string add = src.substr(cur,pos-cur);
    if (clean) {
      string trimed = Trim(add);
      if (trimed.length()>0) {
        dest.push_back(trimed);
      }
    } else {
      dest.push_back(add);
    }
    if (pos==string::npos) {
      break;
    }
    cur=pos+sep.length();
  }
}

string Strings::Join(const vector<string> & strs, const string & sep) {
  string ret;
  for (size_t i = 0; i < strs.size(); i++) {
    if (i > 0) {
      ret.append(sep);
    }
    ret.append(strs[i]);
  }
  return ret;
}

string Strings::ReplaceAll(const string & src, const string & find,
                           const string & replace) {
  vector<string> fds;
  Split(src, find, fds, false);
  return Join(fds, replace);
}

bool Strings::StartsWith(const string & str, const string & prefix) {
  if ((prefix.length() > str.length()) ||
      (memcmp(str.data(), prefix.data(), prefix.length()) != 0)) {
    return false;
  }
  return true;
}

bool Strings::EndsWith(const string & str, const string & suffix) {
  if ((suffix.length() > str.length()) ||
      (memcmp(str.data() + str.length() - suffix.length(),
              suffix.data(),
              suffix.length()) != 0)) {
    return false;
  }
  return true;
}

int64 Strings::LastSeparator(const string & str, char sep) {
  size_t pos = str.rfind(sep);
  if (pos == str.npos) {
    return -1;
  } else {
    return (int64)pos;
  }
}

string Strings::LastPart(const string & str, char sep) {
  int64 pos = LastSeparator(str, sep) + 1;
  return string(str.c_str() + pos, str.length() - pos);
}

const char * Strings::LastPartStart(const string & str, char sep) {
  return str.c_str() + LastSeparator(str, sep) + 1;
}

string Strings::toBase64(const string & str) {
  static const char basis_64[] =
  "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  ssize_t len = str.size();
  string ret((len + 2) / 3 * 4, '\0');
  char * p = const_cast<char*>(ret.c_str());
  ssize_t i;
  for (i = 0; i < len - 2; i += 3) {
    *p++ = basis_64[(str[i] >> 2) & 0x3F];
    *p++ = basis_64[((str[i] & 0x3) << 4) | ((str[i + 1] & 0xF0U) >> 4)];
    *p++ = basis_64[((str[i + 1] & 0xF) << 2) | ((str[i + 2] & 0xC0U) >> 6)];
    *p++ = basis_64[str[i + 2] & 0x3F];
  }
  if (i < len) {
    *p++ = basis_64[(str[i] >> 2) & 0x3F];
    if (i == (len - 1)) {
      *p++ = basis_64[((str[i] & 0x3) << 4)];
      *p++ = '=';
    }
    else {
      *p++ = basis_64[((str[i] & 0x3) << 4) | ((str[i + 1] & 0xF0U) >> 4)];
      *p++ = basis_64[((str[i + 1] & 0xF) << 2)];
    }
    *p++ = '=';
  }
  return ret;
}

string Strings::fromBase64(const string & str) {
  static const uint8 pr2six[256] = {
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 62, 64, 64, 64, 63,
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 64, 64, 64, 64, 64, 64,
    64,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 64, 64, 64, 64, 64,
    64, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
    41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64
  };
  ssize_t last = str.length() - 1;
  while (last >= 0 && pr2six[(uint8)(str[last])] >= 64) {
    last--;
  }
  ssize_t len = last + 1;
  size_t retlen = len / 4 * 3;
  if (len % 4 == 2) {
    retlen += 1;
  } else if (len % 4 == 3) {
    retlen += 2;
  }
  string ret(retlen, '\0');
  const uint8 * bufin = (uint8*)(str.c_str());
  uint8 * bufout = (uint8*)(ret.c_str());
  while (len > 4) {
    *(bufout++) = (uint8) (pr2six[*bufin] << 2 | pr2six[bufin[1]] >> 4);
    *(bufout++) = (uint8) (pr2six[bufin[1]] << 4 | pr2six[bufin[2]] >> 2);
    *(bufout++) = (uint8) (pr2six[bufin[2]] << 6 | pr2six[bufin[3]]);
    bufin += 4;
    len -= 4;
  }
  if (len > 1) {
    *(bufout++) = (uint8) (pr2six[*bufin] << 2 | pr2six[bufin[1]] >> 4);
  }
  if (len > 2) {
    *(bufout++) = (uint8) (pr2six[bufin[1]] << 4 | pr2six[bufin[2]] >> 2);
  }
  if (len > 3) {
    *(bufout++) = (uint8) (pr2six[bufin[2]] << 6 | pr2six[bufin[3]]);
  }
  return ret;
}

string Strings::addslashes(const char *pStr, const char *match) {
  string result;
  while (*pStr) {
    if (strchr(match, *pStr)) {
      result.push_back('\\');
    }
    result.push_back(*pStr);
    ++pStr;
  }
  return result;
}

string Strings::escape(const char *pStr) {
  string result;
  while (*pStr) {
    if (strchr("\"\\'\r\n\t", *pStr)) {
      //bad character, skip
    } else {
      result.push_back(*pStr);
    }
    ++pStr;
  }
  return result;
}

string Strings::escape(const string &in) {
  return escape(in.c_str());
}

void Strings::reverse(string &inStr) {
  std::reverse(inStr.begin(), inStr.end());
}


#ifdef __MACH__

int64 Time::CurrentMonoNano() {
  static mutex lock;
  static mach_timebase_info_data_t tb;
  static uint64 timestart = 0;
  if (timestart == 0) {
    lock.lock();
    if (timestart == 0) {
      mach_timebase_info(&tb);
      timestart = mach_absolute_time();
    }
    lock.unlock();
  }
  uint64 ret = mach_absolute_time() - timestart;
  ret *= tb.numer;
  ret /= tb.denom;
  return (int64)ret;
}

int64 Time::CurrentTimeNano() {
  clock_serv_t cclock;
  mach_timespec_t mts;
  host_get_clock_service(mach_host_self(), CALENDAR_CLOCK, &cclock);
  clock_get_time(cclock, &mts);
  mach_port_deallocate(mach_task_self(), cclock);
  return 1000000000ULL * mts.tv_sec + mts.tv_nsec;
}

#else

int64 Time::CurrentMonoNano() {
  timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return 1000000000 * ts.tv_sec + ts.tv_nsec;
}
int64 Time::CurrentTimeNano() {
  timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  return 1000000000 * ts.tv_sec + ts.tv_nsec;
}

#endif

// thread
uint64 Thread::CurrentId() {
  return (uint64)pthread_self();
}

void Thread::Sleep(uint32 millisec) {
  usleep(millisec * 1000);
}


string demangle(const char * sym) {
#ifdef __MACH__
  string orig(sym);
  auto start = orig.find(" _Z");
  if (start == orig.npos) {
    return orig;
  }
  auto end = orig.find(" ", start+1);
  string cxxmangled = orig.substr(start + 1, end - (start + 1));
  char * buff = new char[256];
  size_t length = 256;
  int status;
  abi::__cxa_demangle(cxxmangled.c_str(), buff, &length, &status);
  string ret = orig.substr(0, start);
  ret.append(" ");
  ret.append(buff);
  delete [] buff;
  ret.append(orig.substr(end));
  ret = Strings::ReplaceAll(ret,
                            "std::__1::basic_string<char, std::__1::char_traits<char>, std::__1::allocator<char> >",
                            "string");
  ret = Strings::ReplaceAll(ret, "std::__1::", "");
  return ret;
#else
  return sym;
#endif
}

void Thread::AddStackTrace(string & dst) {
  void *array[64];
  size_t size;
  size = backtrace(array, 64);
  char ** traces = backtrace_symbols(array, size);
  for (size_t i = 1; i < size; i++) {
    dst.append(demangle(traces[i]));
    dst.append("\n");
  }
  free(traces);
}

void Thread::LogStackTrace(const char * msg) {
  string st;
  AddStackTrace(st);
  LOG("%s\n%s", msg, st.c_str());
}

// log
const char * gLogLevels[] = {
  "DEBUG", "INFO", "WARN", "ERROR", "FATAL"
};

static LogLevel SetupLogLevel() {
  LogLevel ret = LOG_LEVEL_INFO;
  char * s = getenv("LOG_LEVEL");
  if (s != nullptr) {
    for (int i = 0; i < 4; i++) {
      if (strcmp(gLogLevels[i], s) == 0) {
        ret = (LogLevel) i;
        break;
      }
    }
  }
  if (getenv("DEBUG")) {
    ret = LOG_LEVEL_DEBUG;
  }
  return ret;
}

static FILE * SetupLogDevice() {
  FILE * ret = stderr;
  char * s = getenv("LOG_DEVICE");
  if (s != nullptr) {
    if (strcmp("stdout", s)==0) {
      ret = stdout;
    }
    else if (strcmp("stderr", s)==0) {
      ret = stderr;
    }
    else {
      FILE * t = fopen(s, "w+");
      if (t != nullptr) {
        ret = t;
      }
    }
  }
  return ret;
}

LogLevel Log::Level = SetupLogLevel();
FILE * Log::Device = SetupLogDevice();
mutex Log::LogMutex;


static const int BUFFSIZE = 1024 * 1;
void Log::LogMessage(LogLevel level, const char * fmt, ...) {
  timeval log_timer = {0, 0};
  struct tm log_tm;
  gettimeofday(&log_timer, nullptr);
  localtime_r(&(log_timer.tv_sec), &log_tm);
  char buff[BUFFSIZE+2];  // 2: '\n' + '\0'
  int len1 = snprintf(buff, BUFFSIZE,
                      "%02d-%02d-%02d %02d:%02d:%02d.%03d %012llx %s ",
                      log_tm.tm_year % 100,
                      log_tm.tm_mon + 1,
                      log_tm.tm_mday,
                      log_tm.tm_hour,
                      log_tm.tm_min,
                      log_tm.tm_sec,
                      (int32_t)(log_timer.tv_usec / 1000),
                      (unsigned long long)pthread_self(),
                      gLogLevels[level]);
  char * outputbuff = buff;
  int rest = BUFFSIZE - len1;
  va_list al, al2;
  va_start(al, fmt);
  va_copy(al2, al);
  int len2 = vsnprintf(buff + len1, rest + 1, fmt, al);
  va_end(al);
  if (len2 > rest) {
    outputbuff = (char*)malloc(len1 + len2 + 2);
    if (outputbuff) {
      memcpy(outputbuff, buff, len1);
      vsnprintf(outputbuff + len1, len2 + 1, fmt, al2);
    } else {
      // truncate
      len2 = rest;
    }
  }
  va_end(al2);
  outputbuff[len1 + len2] = '\n';
  LogMutex.lock();
  ::fwrite(outputbuff, len1 + len2 + 1, 1, Device);
  ::fflush(Device);
  LogMutex.unlock();
  if (outputbuff != buff) {
    free(outputbuff);
  }
}

static uint32 GetRandomSeed() {
  uint64 seed64 = Time::CurrentTimeNano() ^ Thread::CurrentId();
  return (seed64 >> 32) ^ (seed64 & 0xffffffff);
}

std::mt19937 gRandomGen(GetRandomSeed());
mutex gRandomGock;

uint32 Random::NextUInt32() {
  static std::uniform_int_distribution<uint32> dist;
  ScopeLock slock(gRandomGock);
  return dist(gRandomGen);
}

uint64 Random::NextUInt64() {
  static std::uniform_int_distribution<uint64> dist;
  ScopeLock slock(gRandomGock);
  return dist(gRandomGen);
}

float Random::NextFloat() {
  static std::uniform_real_distribution<float> dist;
  ScopeLock slock(gRandomGock);
  return dist(gRandomGen);
}

double Random::NextDouble() {
  static std::uniform_real_distribution<double> dist;
  ScopeLock slock(gRandomGock);
  return dist(gRandomGen);
}

void Random::NextBytes(uint32 len, string & bytes) {
  static std::uniform_int_distribution<uint64> dist;
  ScopeLock slock(gRandomGock);
  bytes.resize(len);
  uint8 * data = (uint8*)bytes.data();
  uint64 value;
  for (uint32 i = 0; i < len; i++) {
    if (i % 8 == 0) {
      value = dist(gRandomGen);
    }
    data[i] = (uint8)(value >> (i*8));
  }
}

void Random::NextVisableBytes(uint32 len, string & bytes) {
  static std::uniform_int_distribution<uint64> dist;
  ScopeLock slock(gRandomGock);
  bytes.resize(len);
  uint8 * data = (uint8*)bytes.data();
  uint64 value;
  for (uint32 i = 0; i < len; i++) {
    if (i % 8 == 0) {
      value = dist(gRandomGen);
    }
    data[i] = ((uint8)(value >> (i*8)) % (126-33)) + 33;
  }
}

void Random::NextUUID(string & uuid) {
  NextBytes(16, uuid);
  // set version
  // must be 0b0100xxxx
  uuid[6] &= 0x4F; //0b01001111
  uuid[6] |= 0x40; //0b01000000
  // set variant
  // must be 0b10xxxxxx
  uuid[8] &= 0xBF;
  uuid[8] |= 0x80;
}

const char * Exception::TypeToString(int type) {
  return strerror(type);
}


// true for easy debug
static bool GLogException = false;

void Exception::SetLogException(bool logException) {
  GLogException = logException;
}

Exception::Exception(int type, const char * where,
                     const string & what) {
  _type = type;
  _reason = what;
  if (where != nullptr) {
    _stackTrace = where;
    _stackTrace.append("\n");
  }
  Thread::AddStackTrace(_stackTrace);
  if (GLogException || getenv("DEBUG")) {
    LOG_WARN("%s: %s thrown at:\n%s",
             TypeToString(_type), _reason.c_str(), _stackTrace.c_str());
  }
}

Exception::Exception(const char * where, const string & what) {
  _type = ERROR_UNKNOWN;
  _reason = what;
  if (where != nullptr) {
    _stackTrace = where;
    _stackTrace.append("\n");
  }
  Thread::AddStackTrace(_stackTrace);
  if (GLogException || getenv("DEBUG")) {
    LOG_WARN("%s: %s thrown at:\n%s",
             TypeToString(_type), _reason.c_str(), _stackTrace.c_str());
  }
}

Exception::Exception(const string & what) {
  _type = ERROR_UNKNOWN;
  _reason = what;
  Thread::AddStackTrace(_stackTrace);
  if (GLogException || getenv("DEBUG")) {
    LOG_WARN("%s: %s thrown at:\n%s",
             TypeToString(_type), _reason.c_str(), _stackTrace.c_str());
  }
}

void Exception::logWarn() {
  LOG_WARN("%s: %s thrown at:\n%s",
           TypeToString(_type), _reason.c_str(), _stackTrace.c_str());
}

void Exception::logInfo() {
  LOG_INFO("%s: %s thrown at:\n%s",
           TypeToString(_type), _reason.c_str(), _stackTrace.c_str());
}



Config Config::GConfig;

Config::Config() {}
Config::~Config() {}

void Config::parseConfig(const char * filename, bool required) {
  FILE * fin = fopen(filename, "r");
  if (!fin) {
    if (required) {
      THROW_EXCEPTION_EX(EINVAL, "can't load config file: %s", filename);
    } else {
      LOG_WARN("can't find config file: %s, skip", filename);
      return;
    }
  }
  LOG("Loading config from %s", filename);
  char buff[1024];
  while (true) {
    char * line = fgets(buff, 1024, fin);
    if (line == NULL) {
      if (!feof(fin)) {
        THROW_EXCEPTION_EX(EINVAL, "load config file error: %s", filename);
      }
      break;
    }
    string l(line);
    if (l[l.length()-1] == '\n') {
      l = l.substr(0, l.length()-1);
    }
    l = Strings::Trim(l);
    if (l.length() <= 1) {
      continue;
    }
    if (l[0] == '#') {
      continue;
    }
    auto pos = l.find("=");
    if (pos == l.npos) {
      THROW_EXCEPTION_EX(EINVAL, "load config file(%) error line:%s", filename, line);
    }
    kvs_[l.substr(0, pos)] = l.substr(pos+1);
  }
  fclose(fin);
}

void Config::parseArgs(int argc, char ** argv) {
  for (int i = 0 ; i < argc; i++) {
    string l = Strings::Trim(argv[i]);
    auto pos = l.find("=");
    if (pos != l.npos) {
      kvs_[l.substr(0, pos)] = l.substr(pos + 1);
    }
  }
}

string Config::get(const string & key) {
  auto itr = kvs_.find(key);
  if (itr == kvs_.end()) {
    THROW_EXCEPTION_EX(EINVAL, "Config::get(%s) not found", key.c_str());
  } else {
    return itr->second;
  }
}

int64 Config::getInt(const string & key) {
  auto itr = kvs_.find(key);
  if (itr == kvs_.end()) {
    THROW_EXCEPTION_EX(EINVAL, "Config::getInt(%s) not found", key.c_str());
  } else {
    return Strings::toInt(itr->second);
  }
}

string Config::get(const string & key, const string & defaultValue) {
  auto itr = kvs_.find(key);
  if (itr == kvs_.end()) {
    return defaultValue;
  } else {
    return itr->second;
  }
}

int64 Config::getInt(const string & key, int64 defaultValue) {
  auto itr = kvs_.find(key);
  if (itr == kvs_.end()) {
    return defaultValue;
  } else {
    return Strings::toInt(itr->second);
  }
}

double Config::getDouble(const string & key) {
  auto itr = kvs_.find(key);
  if (itr == kvs_.end()) {
    THROW_EXCEPTION_EX(EINVAL, "Config::getDouble(%s) not found", key.c_str());
  } else {
    return Strings::toDouble(itr->second);
  }
}

double Config::getDouble(const string & key, double defaultValue) {
  auto itr = kvs_.find(key);
  if (itr == kvs_.end()) {
    return defaultValue;
  } else {
    return Strings::toDouble(itr->second);
  }
}

bool Config::getBool(const string & key) {
  auto itr = kvs_.find(key);
  if (itr == kvs_.end()) {
    THROW_EXCEPTION_EX(EINVAL, "Config::getBool(%s) not found", key.c_str());
  } else {
    return Strings::toBool(itr->second);
  }
}

bool Config::getBool(const string & key, bool defaultValue) {
  auto itr = kvs_.find(key);
  if (itr == kvs_.end()) {
    return defaultValue;
  } else {
    return Strings::toBool(itr->second);
  }
}




