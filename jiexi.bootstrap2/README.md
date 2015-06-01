# Explorer - 解析器 - 初试化加速专用

## 初始化时注意点

1. 因并发需求，程序未使用数据事务，勿在程序允许中强制`kill -9`，使用`Ctrl + C`，或者`kill \`cat tparser.pid\``
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
apt-get install -y libboost-dev libboost-thread-dev libboost-system-dev libboost-regex-dev libboost-filesystem-dev openssl libssl-dev libmysqlclient-dev libcurl4-openssl-dev

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
