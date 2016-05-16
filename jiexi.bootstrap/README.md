# Explorer - 解析器 - 初试化加速专用

## 机器要求

* 内存：64G以上
  * 处理高度390930时，pre\_parser需要内存接近40G
* CPU：8核以上


## 初始化时注意点

1. raw block/tx等原始数据中不允许出现孤块，无法处理
1. 按照块为单位进行处理

## Install Guide
### ubuntu 12.04 / 14.04

```
# 参考 jiexi/README.md 安装相关依赖库后，再进行编译

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

### 生成原始块数据

```
cd build
cp ../tools/block_importer.sh .
cp ../tools/bootstrap.sh .

# 修改 tparser.bootstrap.conf 配置
# 修改 block_importer.sh 脚本中最大高度配置
./block_importer.sh
```

### 初始化

下面几个步骤可以直接执行：

```
./bootstrap.sh
```

**或分步执行**

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

### 初始化v3数据库

```
cd Explorer/install
./generate_initialize_sql.sh > v3_initialize.sql
# 创建表
mysql -h xxx -u xxxx -p explorer_v3_main_db < v3_initialize.sql
```
