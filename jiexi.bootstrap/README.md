# Explorer - 解析器 - 初试化加速专用

## 初始化时注意点

1. raw block/tx等原始数据中不允许出现孤块，无法处理
1. 按照块为单位进行处理

## Install Guide
### ubuntu 12.04 / 14.04

```
# oh my zsh (optional)
apt-get install zsh git
wget --no-check-certificate http://install.ohmyz.sh -O - | sh

# env
apt-get install -y cmake build-essential autotools-dev libtool autoconf automake

# pkgs
apt-get install -y libboost-dev libboost-thread-dev libboost-system-dev libboost-regex-dev libboost-filesystem-dev openssl libssl-dev libmysqlclient-dev libcurl4-openssl-dev libgoogle-perftools-dev

#
# build
#
cd Explorer/jiexi
mkdir -p build
cd build
cmake ..
make -j2

#
# test
#
cd Explorer/jiexi/build
cp ../test/unittest.conf .
# run all test case
./unittest

# run one test case
./unittest --gtest_filter=Common\*
```

## 运行

第一步，需要初始化`address`和`transaction`，分别对应程序：`pre_addr`, `pre_tx`。这两个初始化程序可同时运行（内存必须64G以上）。

```
# 初始化地址
./pre_addr -c tparser.bootstrap.conf -l pre_addr.log

# 初始化交易
./pre_tx -c tparser.bootstrap.conf -l pre_tx.log
```

第二步，生成CSV的DB导入文件

```
./pre_parser -c tparser.bootstrap.conf -l tparser.log
```
