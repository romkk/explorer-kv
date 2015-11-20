# Key Value DB

## 全局前缀分配

编码 | 类型 | Key | 说明 
------ | ------ | ------
00 | transaction | 00_{tx_hash} | tx raw hex
01 | transaction | 01_{tx_hash} | tx object (json)
02 | transaction | 02_{tx_hash} | spent txs, key当前交易hash，value花费交易hash
10 | block | 10_{block_height} | block height -> hash
11 | block | 11_{block_hash} | block object (json)
12 | block | 12_{block_hash}_{n} | block transactions, n为批次号，每1000条为一个批次
20 | address | 20_{address} | address object (json) 
21 | address | 21_{address}_{010index} | address txs list
22 | address | 22_{address}_{tx_hash} | address txhash -> address tx idx
23 | address | 23_{address}_{010index} | address unspent txs list
24 | address | 24_{address}_{tx_hash}_{position} | 对应该交易产生的某个地址的未花费index
30 | double spent tx | 30_{tx_hash}_{position} | 双花交易，数组

