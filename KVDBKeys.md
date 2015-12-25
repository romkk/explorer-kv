# Key Value DB

## 全局前缀分配

编码 | 类型 | Key | Value | 说明 
------ | ------ | ------|--------|------
00 | transaction | 00\_{tx\_hash} | Binary | tx raw hex，一次插入不再变更
01 | transaction | 01\_{tx\_hash} | FlatBuffer | tx object，一次插入不再变更
02 | transaction | 02\_{tx\_hash}\_{position} | FlatBuffer | spent txs, value记录该hash被谁花费了
03 | transaction | 03\_{tx\_hash} | FlatBuffer | 未确认交易，hash、大小等信息
10 | block | 10\_{010block\_height} | string | block height -> hash
11 | block | 11\_{block\_hash} | FlatBuffer | block object
12 | block | 12\_{block\_hash}\_{n} | string | block transactions, n为批次号，每500条为一个批次, n从零开始
13 | block | 13\_{010height} | string | 当前高度的孤块Hash值字符串拼接，均为孤块哈希
14 | block | 14\_{010timestamp}_{010height} | string | 块按照当前最大时间戳(`curr_max_timestamp`)的索引，不含孤块数据
15 | block | 15\_{block\_hash} | FlatBuffer | 块对应的矿池信息
20 | address | 20\_{address} | FlatBuffer | address object
21 | address | 21\_{address}\_{010index} | FlatBuffer | 地址交易，address txs list
22 | address | 22\_{address}\_{tx\_hash} | string | 地址交易索引，int32_t string, address txhash -> address tx idx
23 | address | 23\_{address}\_{010index} | FlatBuffer | 未确认，address unspent txs list
24 | address | 24\_{address}\_{tx\_hash}\_{position} | FlatBuffer | 未确认索引，对应交易产生的某个地址的未花费index，position表示位于输出的索引号
30 | double spent tx | 30\_{tx\_hash}\_{position} | | 双花交易，数组
90 | system, meta, counter | 90\_{key} | | 各种系统用的计数器，meta数据等


## 90

 key | value | desc
-----|-------|-----
`90_tparser_unconfirmed_txs_size`  | string | int64_t字符串，未确认交易体积
`90_tparser_unconfirmed_txs_count` | string | int32_t字符串，未确认交易数量
`90_tparser_txlogs_offset_id` | string | int32_t字符串，最后成功消费的txlogs2 ID

