# Key Value DB

## 全局前缀分配

编码 | 类型 | 说明 
------ | ------ | ------
00 | transaction | tx raw hex
01 | transaction | tx object (json)
02 | transaction | spent txs
10 | block | block height -> hash
11 | block | block object (json)
20 | address | address object (json) 
21 | address | address txs list
22 | address | address unspent txs list
30 | unspent outputs | unspent outputs txs

