# BITMAIN Block Explorer API Reference

## API 前缀

## 鉴权

### 如何获取 Access Key

### 鉴权方法

## 请求编码

## 常见异常

## API List

### Single Block

#### Request

* Get Block By Height

        GET /block-height/${block_height}

    |参数|描述|位置|必须|数据类型|
    |---|---|---|---|---|
    |block_height|块高度|Path|√|Int|


* Get Block By Hash

        GET /rawblock/${block_hash}

    |参数|描述|位置|必须|数据类型|
    |---|---|---|---|---|
    |block_hash|块哈希|Query|√|String|


#### Response

|字段名称|类型|含义|备注|
|---|---|---|---|

```JSON
{
   "main_chain" : true,
   "ver" : 1,
   "relayed_by" : "108.60.208.156",
   "n_tx" : 22,
   "received_time" : 1322131301,
   "mrkl_root" : "935aa0ed2e29a4b81e0c995c39e06995ecce7ddbebb26ed32d550a72e8200bf5",
   "nonce" : 2964215930,
   "height" : 154595,
   "time" : 1322131230,
   "size" : 9195,
   "hash" : "0000000000000bae09a7a393a8acded75aa67e46cb81f7acaa5ad94f9eacd103",
   "tx" : [],
   "block_index" : 818044,
   "bits" : 437129626,
   "prev_block" : "00000000000007d0f98d9edca880a6c124e25095712df8952e0439ac7409738a",
   "txs": [-- list of txs --]
}
```

### Single transaction

#### Request

    GET /rawblock/{$tx_hash}
    
|参数|描述|位置|必须|数据类型|
|---|---|---|---|---|
|tx_hash|交易哈希|Query|√|String|
|scripts|返回值是否包含输入和输出脚本，默认为`false`|Query|✗|Boolean|

#### Response

```JSON
{
   "tx_index" : "12563028",
   "inputs" : [
      {
         "script" : "76a914641ad5051edd97029a003fe9efb29359fcee409d88ac",
         "prev_out" : {
            "value" : "100000000",
            "hash" : "a3e2bcc9a5f776112497a32b05f4b9e5b2405ed9",
            "n" : "2",
            "tx_index" : "12554260"
         }
      }
   ],
   "hash" : "b6f6991d03df0e2e04dafffcd6bc418aac66049e2cd74b80f14ac86db1e3f0da",
   "vin_sz" : 1,
   "size" : 258,
   "vout_sz" : 2,
   "ver" : 1,
   "out" : [
      {
         "hash" : "29d6a3540acfa0a950bef2bfdc75cd51c24390fd",
         "value" : "98000000",
         "script" : "76a914641ad5051edd97029a003fe9efb29359fcee409d88ac"
      },
      {
         "script" : "76a914641ad5051edd97029a003fe9efb29359fcee409d88ac",
         "hash" : "17b5038a413f5c5ee288caa64cfab35a0c01914e",
         "value" : "2000000"
      }
   ],
   "relayed_by" : "64.179.201.80",
   "lock_time" : "Unavailable",
   "block_height" : 12200
}
```

### Single Address

#### Request

    GET /address/${bitcoin_address}
    
|参数|描述|位置|必须|数据类型|
|---|---|---|---|---|
|bitcoin_address|比特币地址|Path|√|String|
|offset|返回结果集跳过的个数，默认为`0`|Query|✗|Int|
|timestamp|返回结果集开始的时间戳，默认为查询时间|Query|✗|Int|
|limit|返回结果集个数，默认为`50`|Query|✗|Int|
|sort|排序方式，可选为`desc`和`asc`，默认为`desc`|Query|✗|String|

**注意：`offset`与`timestamp`同时使用时，将忽略`timestamp`。**

#### Response

```JSON
{
    "hash160":"660d4ef3a743e3e696ad990364e555c271ad504b",
    "address":"1AJbsFZ64EpEfS5UAjAfcUG8pH8Jn3rn1F",
    "n_tx":17,
    "n_unredeemed":2,
    "total_received":1031350000,
    "total_sent":931250000,
    "final_balance":100100000,
    "txs":[--Array of Transactions--]
}
```
    
### Multi Address

#### Request

    GET /multiaddr
    
|参数|描述|位置|必须|数据类型|
|---|---|---|---|---|
|active|多个比特币地址，使用 <code>&#124;</code> 分隔|Query|√|string|

#### Response

```JSON
{
   "addresses" : [
      {
         "final_balance" : 1400000000,
         "address" : "1A8JiWcwvpY7tAopUkSnGuEYHmzGYfZPiq",
         "total_sent" : 1000000,
         "total_received" : 1401000000,
         "n_tx" : 4,
         "hash160" : "641ad5051edd97029a003fe9efb29359fcee409d"
      },
      {
         "hash160" : "ddbeb8b1a5d54975ee5779cf64573081a89710e5",
         "n_tx" : 0,
         "total_received" : 0,
         "total_sent" : 0,
         "address" : "1MDUoxL1bGvMxhuoDYx6i11ePytECAk9QK",
         "final_balance" : 0
      }
   ]
}

```

### Unspend outputs

#### Request

    GET /unspent
    
|参数|描述|位置|必须|数据类型|
|---|---|---|---|---|
|active|多个比特币地址，使用`|`分隔|Query|√|string|

#### Response

```JSON
{
	"unspent_outputs":[
		{
			"tx_age":"1322659106",
			"tx_hash":"e6452a2cb71aa864aaa959e647e7a4726a22e640560f199f79b56b5502114c37",
			"tx_index":"12790219",
			"tx_output_n":"0",	
			"script":"76a914641ad5051edd97029a003fe9efb29359fcee409d88ac", (Hex encoded)
			"value":"5000661330"
		}
	]
}
```


### Latest Block

#### Request

    GET /latestblock
    
#### Response

```JSON
{
	"hash":"0000000000000538200a48202ca6340e983646ca088c7618ae82d68e0c76ef5a",
	"time":1325794737,
	"block_index":841841,
	"height":160778,
	"txIndexes":[13950369,13950510,13951472]
}
```

### Unconfirmed Transactions

#### Request

    GET /unconfirmed-transactions
    
#### Response

```JSON
{
	"txs":[--Array of Transactions--]
}
```
