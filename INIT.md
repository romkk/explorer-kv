# 初始化

以testnet3为例，main初始化过程类似。

## 初试化数据

```
# 创建目录
mkdir jiexi.bootstrap.testnet3
cd jiexi.bootstrap.testnet3

# 拷贝一些脚本
cp ../jiexi.bootstrap/tools/bootstrap.sh .
cp ../jiexi.bootstrap/tools/block_importer.sh .

# 创建可执行程序链接
ln -s ../jiexi.bootstrap/build/compactdb
ln -s ../jiexi.bootstrap/build/pre_parser
ln -s ../jiexi.bootstrap/build/blkimport
ln -s ../jiexi.bootstrap/build/pre_addr
ln -s ../jiexi.bootstrap/build/pre_tx

# 编辑配置i文件
cp ../jiexi.bootstrap/src/tparser/tparser.bootstrap.conf .
vim tparser.bootstrap.conf
```

生成基础数据

```
cd jiexi.bootstrap.testnet3
mkdir block_importer

# 导出块数据，若存在部分数据，则单独运行命令指定范围按需导出
./block_importer.sh

# 初试化RocksDB
./bootstrap.sh
```

导入SQL，建库，并导入SQL建表

```
cd install
./generate_initialize_sql.sh > v3_initialize.sql
mysql -hxxxx -uxxxxx -p explorer_v3_testnet3_db < v3_initialize.sql
mysql -hxxxx -uxxxxx -p notify_db_testnet3 < init_notify_db.sql
```

运行

```
mkdir jiexi.testnet3
cd jiexi.testnet3

ln -s ../jiexi/build/apiserver
ln -s ../jiexi/build/log1producer
ln -s ../jiexi/build/log2producer
ln -s ../jiexi/build/tparser

cp ../jiexi/src/log1producer/log1producer.conf .
cp ../jiexi/src/log2producer/log2producer.conf .
cp ../jiexi/src/tparser/tparser.conf .

# 编辑配置文件：log1producer.conf，log2producer.conf，tparser.conf

#
# 一般用supervise或者tmux运行程序
#
# 1. 运行 log1producer
./log1producer -c "log1producer.conf" -l "log1producer.log"

# 2. 运行 log2producer
./log2producer -c "log2producer.conf" -l "log2producer.log"

# 3. 运行 tparser
./tparser -c "tparser.conf" -l "tparser.log"

```
