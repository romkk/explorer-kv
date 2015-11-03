# Explorer - 解析器

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

## 部署
### logrotate.d

假设日志目录为：`/work/Explorer/jiexi/build/supervise_tparser/tparser.log`，则 logrotate.d 的配置文件为：

```
$ cat /etc/logrotate.d/tparser
/work/Explorer/jiexi/build/supervise_tparser/tparser.log {
    daily
    rotate 14
    missingok
    copytruncate
}
```

testnet3 配置类似，请自行复制一份修改日志路径即可。
