# Key Value DB

## 全局前缀分配

编码 | 类型 | Key | Value | 说明 
------ | ------ | ------
00 | transaction | 00\_{tx_hash} | Binary | tx raw hex，一次插入不再变更
01 | transaction | 01\_{tx_hash} | FlatBuffer | tx object，一次插入不再变更
02 | transaction | 02\_{tx_hash}\_{position} | FlatBuffer | spent txs, value记录该hash被谁花费了
10 | block | 10\_{010block_height} | string | block height -> hash
11 | block | 11\_{block_hash} | FlatBuffer | block object
12 | block | 12\_{block_hash}\_{n} | string | block transactions, n为批次号，每500条为一个批次, n从零开始
20 | address | 20\_{address} | FlatBuffer | address object
21 | address | 21\_{address}\_{010index} | FlatBuffer | address txs list
22 | address | 22\_{address}\_{tx_hash} | FlatBuffer | address txhash -> address tx idx
23 | address | 23\_{address}\_{010index} | FlatBuffer | address unspent txs list
24 | address | 24\_{address}\_{tx_hash}\_{position} | FlatBuffer | 对应该交易产生的某个地址的未花费index，position表示位于输出的索引号
30 | double spent tx | 30\_{tx_hash}\_{position} | | 双花交易，数组
90 | system, meta, counter | 90_{key} | | 各种系统用的计数器，meta数据等


## 90

 key | value | desc
-----|-------|-----
90_tparser_unconfirmed_txs_size  | string | int64_t字符串，未确认交易体积
90_tparser_unconfirmed_txs_count | string | int32_t字符串，未确认交易数量

