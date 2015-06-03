# BITMAIN Block Explorer API Reference

该 API 的设计目标是提供一个高性能的区块链数据查询接口，并尽量兼容 [blockchain.info](https://blockchain.info/api/blockchain_api) 的 API 约定，降低用户的使用成本。

## URI

* 协议： `HTTP`
* Host： `api.explorer.btc.com`
* API 版本： `/{api_version}`，可选版本为 `v1`
* 请求类型：`GET`

以下如无特别说明，所有的 API 调用时均使用以上 URI 前缀，如获取最新区块信息：

    GET http://api.explorer.btc.com/v1/latestblock

## 访问频率限制

API 服务根据访问者 IP 限制访问频率。

TODO

## 响应格式

API 请求的响应结果均为`JSON`格式、`UTF8`编码，并开启了 Gzip 压缩。一个典型的响应如下：

```
HTTP/1.1 200 OK
Content-Encoding: gzip
Content-Type: application/json
Date: Wed, 03 Jun 2015 07:04:20 GMT
Connection: keep-alive;
Transfer-Encoding: chunked
```

## 异常

TODO

## API List

常见参数说明：

1. 支持分页的接口使用`offset`和`limit`进行分页。如查询第 3 页，每页 20 条记录，则参数应为`?offset=40&limit=20`；
1. 时间单位均为 UTC 时间；

### Single Block

#### Request

* Get Block By Height

        GET /block-height/${block_height}

* Get Block By Hash

        GET /rawblock/${block_hash}
        
|参数|描述|位置|必须|数据类型|
|---|---|---|---|---|
|block_height|块高度|Path|√|Int|
|fulltx|是否显示详细的交易信息，默认为`false`|Query|✗|Boolean|
|offset|返回结果集跳过的个数，默认为`0`|Query|✗|Int|
|limit|返回结果集个数，要求大于`1`小于`50`，默认为`50`|Query|✗|Int|

#### Response

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
   "block_index" : 818044,
   "bits" : 437129626,
   "prev_block" : "00000000000007d0f98d9edca880a6c124e25095712df8952e0439ac7409738a",
   "txs": [-- list of txs --]
}
```

### Single transaction

#### Request

* Get Transaction By Hash

        GET /rawblock/{$tx_hash}

    |参数|描述|位置|必须|数据类型|
    |---|---|---|---|---|
    |tx_hash|交易哈希|Path|√|String|
    |scripts|返回值是否包含输入和输出脚本，默认为`false`|Query|✗|Boolean|

* Get Transaction By Tx_id

        GET /rawblock/${tx_id}

    |参数|描述|位置|必须|数据类型|
    |---|---|---|---|---|
    |tx_id|交易 id|Path|√|String|
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
|limit|返回结果集个数，要求大于`1`小于`50`，默认为`50`|Query|✗|Int|
|sort|排序方式，可选为`desc`和`asc`，默认为`desc`|Query|✗|String|

该 API 可用于查询单个地址某个时间范围内的交易列表。`timestamp`字段指定了查询时间点。

如：

1. 查询当前最新 10 条交易记录：

        GET /address/2N66DDrmjDCMM3yMSYtAQyAqRtasSkFhbmX?limit=10

1. 查询截至 6 月 1 日的 20 条交易记录：

        GET /address/2N66DDrmjDCMM3yMSYtAQyAqRtasSkFhbmX?timestamp=1433116800&limit=20

1. 查询 5 月 1 日至 6 月 4 日的交易记录：

        GET /address/2N66DDrmjDCMM3yMSYtAQyAqRtasSkFhbmX?timestamp=1430438400&sort=asc

   该请求将从 5 月 1 日 0 点起返回交易记录，默认返回 50 条；这里由客户端程序负责持续拼接数据，即，如果上述请求返回的最后一条记录是 5 月 3 日 3 点 0 分，则下一条请求应当是：

        GET /address/2N66DDrmjDCMM3yMSYtAQyAqRtasSkFhbmX?timestamp=1430622000&sort=asc

   直到接收到的数据达到 6 月 4 日 0 时停止。

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
|active|多个比特币地址，使用 <code>&#124;</code> 分隔，最多 128 个地址|Query|√|string|

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

### Unspent outputs

#### Request

    GET /unspent
    
|参数|描述|位置|必须|数据类型|
|---|---|---|---|---|
|active|多个比特币地址，使用 <code>&#124;</code> 分隔，最多 128 个地址|Query|√|string|

#### Response

```JSON
{
    "unspent_outputs": [
        {
            "tx_hash": "d93fa00752e4d8acb16a8508349be7ff5c00ce3c9fae275dde00e4f92f8f6168",
            "tx_hash_big_endian": "68618f2ff9e400de5d27ae9f3cce005cffe79b3408856ab1acd8e45207a03fd9",
            "tx_index": 88309954,
            "tx_output_n": 0,
            "value": 309299180,
            "value_hex": "126f87ec",
            "confirmations": 6
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