# Explorer - 解析器

## 依赖

* FlatBuffers v1.2.0
* RapidJSON   v1.0.2
* RocksDB     版本见 CMakeLists.txt

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

# for rocksdb
apt-get install -y libgflags-dev libsnappy-dev zlib1g-dev libbz2-dev

# evhtp
apt-get install libevent-dev
wget https://github.com/ellzey/libevhtp/archive/1.2.11.tar.gz -O libevhtp-1.2.11.tar.gz
tar zxvf libevhtp-1.2.11.tar.gz
cd libevhtp-1.2.11/build
cmake -DEVHTP_DISABLE_SSL=ON -DEVHTP_DISABLE_REGEX=ON -DEVHTP_BUILD_SHARED=ON ..
make
make install

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

假设日志目录为：`/work/Explorer/jiexi/build/*.log`，则 logrotate.d 的配置文件如下，支持指定多个日志路径。

```
$ cat /etc/logrotate.d/tparser-main
/work/Explorer/jiexi/build/*.log
/work/Explorer/jiexi/build/other_path/other.log
{
    daily
    rotate 7
    compress
    copytruncate
    nocreate
    delaycompress
    notifempty
}
```

testnet3 配置类似，请自行复制一份修改日志路径即可。


## 运行

v3依然采用的数据库，来存放txlogs记录，由于table.raw\_txs\_xxxx特殊情况，需要手动清理：

1. 停止 log1producer
2. 保证 log2producer 已经完全消费log1之后，停止 log2producer
3. 等待tparser，等完全消费了log2后，手动清空所有表： table.raw\_txs\_xxxx
4. 启动log1producer，启动log2producer

之所以采用上述步骤，是因为log2producer在插入每条txlogs日志时，会保障对应的rawtx一定存在。需要设置数据库容量报警，防止空间不够，此步骤可以每3个月执行一次。