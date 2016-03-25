# 事件通知

`tparser`输出日志结构：

```
DATETIME,TYPE,ITEM_STR\n
```

`TYPE`定义

 值 | 类型 | 说明 
-----|------|------
10 | 块 | 高度增一
11 | 块 | 高度减一
20 | 交易 | 接收
21 | 交易 | 确认
22 | 交易 | 反确认。所包含的块回退了，交易变为未确认态
23 | 交易 | 拒绝。发现冲突交易，该交易无效

## 块日志格式

```
DATETIME,TYPE,HEIGHT,BLOCK_HASH\n
```

### 示例

```
# 新增一个块
2016-01-01 12:13:14,10,401656,000000000000000004dd73b0dc84ae2a7bb790d92a3fb457cafca2c221206aa0

# 回退一个块
2016-01-01 12:13:14,11,401626,000000000000000002614acdf9e68a2d5b1d5782cf54bceecba33d1760313b40
```


## 交易日志格式

```
DATETIME,TYPE,ADDRESS,HEIGHT,TX_HASH,AMOUNT\n
```

`AMOUNT`单位为聪，类型INT64.

### 示例

```
# 接收到一个交易, 此时高度默认为-1
2016-01-01 12:13:14,20,18KsvEx5zhNtsatBsTqDvKV5wk73GwSdXx,-1,f3f13a843b8f69a850ad8e45becae5ccd9cc3f5c85f5eb78a54bb9971c50ed9b,725865

# 确认一个交易, 此时高度为所在块高度
2016-01-01 12:13:14,21,18KsvEx5zhNtsatBsTqDvKV5wk73GwSdXx,401626,f3f13a843b8f69a850ad8e45becae5ccd9cc3f5c85f5eb78a54bb9971c50ed9b,725865
```