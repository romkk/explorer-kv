# BITMAIN Block Explorer API Reference

该 API 的设计目标是提供一个高性能的区块链数据查询接口，并尽量兼容 [blockchain.info](https://blockchain.info/api/blockchain_api) 的 API 约定，降低用户的使用成本。

## URI

* 协议： `HTTPS`
* LiveNet Host： `chain.bitmain.com`
* Testnet3 Host:   `tchain.bitmain.com`
* API 版本： `/api/{version}`，当前可选版本为 `v1`
* 请求类型：`GET`

以下如无特别说明，所有的 API 调用时均使用以上 URI 前缀，如获取最新区块信息：

    GET http://{$host}/${path}/latestblock

## 访问频率限制

API 服务根据访问者 IP 限制访问频率。

TODO

## 响应格式

API 请求的响应结果均为`JSON`格式、`UTF8`编码。为了节省流量，推荐在 Request Header 中加入`Accept-Encoding: gzip`字段，开启 Gzip 压缩。

一个典型的响应如下：

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
1. 时间单位均为 UTC 。

### Single Block

#### Request

* Get Block By Block Id

        GET /rawblock/${block_id}

    |参数|描述|位置|必须|数据类型|
    |---|---|---|---|---|
    |block_id|块内部 id|Path|√|Int|
    |fulltx|是否显示详细的交易信息，默认为`false`|Query|✗|Boolean|
    |offset|返回结果集跳过的个数，默认为`0`|Query|✗|Int|
    |limit|返回结果集个数，要求大于`1`小于`50`，默认为`50`|Query|✗|Int|

* Get Block By Hash

        GET /rawblock/${block_hash}

    |参数|描述|位置|必须|数据类型|
    |---|---|---|---|---|
    |block_hash|块哈希|Path|√|Int|
    |fulltx|是否显示详细的交易信息，默认为`false`|Query|✗|Boolean|
    |offset|返回结果集跳过的个数，默认为`0`|Query|✗|Int|
    |limit|返回结果集个数，要求大于`1`小于`50`，默认为`50`|Query|✗|Int|

* Get Block By Height

        GET /block-height/${block_height}

    |参数|描述|位置|必须|数据类型|
    |---|---|---|---|---|
    |block_height|块高度|Path|√|Int|

    注意：该接口不支持`fulltx`、`offset`和`limit`。


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

### Transaction

#### Request

* Get Transaction By Hash

        GET /rawtx/${tx_hash}

    |参数|描述|位置|必须|数据类型|
    |---|---|---|---|---|
    |tx_hash|交易哈希|Path|√|String|

* Get Transaction By Tx_id

        GET /rawtx/${tx_id}

    |参数|描述|位置|必须|数据类型|
    |---|---|---|---|---|
    |tx_id|交易 id|Path|√|String|

* [PRIVATE] Get Multiple Transactions

        GET /rawtx/${tx_id},${tx_id}...

    可以传入多个`tx_id`，使用`,`分隔。

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

### MultiAddress TxList

#### Request

```
GET /address-tx/
```

|参数|描述|位置|必须|数据类型|
|---|---|---|---|---|
|active|多个比特币地址，使用 <code>&#124;</code> 分隔，最多 256 个地址|Query|√|string|
|timestamp|返回结果集开始的时间戳，默认为查询时间|Query|✗|Int|
|limit|返回结果集个数，要求大于`1`小于`50，默认为`50`|Query|✗|Int|
|sort|排序方式，可选为`desc`和`asc`，默认为`desc`|Query|✗|String|

#### Response

```
[
    {
        "ver": 1,
        "inputs": [
            {
                "sequence": 4294967295,
                "script": "483045022100b9aab6f29265674b99a79adc26e9d4fc443077b1ec33adb682c21493b7bfdf570220173a06f4a3a9b47a8af543f9762fbe52aa39ef3d204ed1ee4f6a49bad2e8d07d012102c2ce65118ca9d9c39ce8e71edc39d17d9800ae3c0089931cb5e7e4a5ac98ff7b",
                "prev_out": {
                    "tx_index": 40000046077,
                    "addr": [
                        "mtJL8KeTugcf2YCqvxbFatUbNYDywBFfNR"
                    ],
                    "value": 2500000010,
                    "n": 0
                }
            }
        ],
        "block_height": 485338,
        "out": [
            {
                "spent": false,
                "tx_index": 4000068047,
                "addr": [
                    "mpZM35ZsEFQ7djDZL4UskcGmrVebsHAm76"
                ],
                "value": 100000000,
                "n": 0,
                "script": "76a914632cf2a914c58596ab5238c4162fb81a733654ed88ac"
            },
            {
                "spent": false,
                "tx_index": 4000068047,
                "addr": [
                    "mtJL8KeTugcf2YCqvxbFatUbNYDywBFfNR"
                ],
                "value": 2399990010,
                "n": 1,
                "script": "76a9148c3673c9d3744197ea3c3f90d5b6437d393764ff88ac"
            }
        ],
        "lock_time": 0,
        "size": 226,
        "time": 1435313711,
        "tx_index": 4000068047,
        "hash": "dbf23dc6101cafacaf3acec615864b4a82be7fe71802ff0bc19bd0ea01a6e784",
        "vin_sz": 1,
        "vout_sz": 2,
        "is_coinbase": false,
        "fee": 10000,
        "total_in_value": 2500000010,
        "total_out_value": 2499990010,
        "confirmations": 1376
    }
]
```

### Unspent

#### Request

```
GET /unspent
```

|参数|描述|位置|必须|数据类型|
|---|---|---|---|---|
|active|多个比特币地址，使用 <code>&#124;</code> 分隔，最多 128 个地址|Query|√|string|
|offset|返回结果集跳过的个数，默认为`0`|Query|✗|Int|
|limit|返回结果集个数，要求大于`1`小于`200`，默认为`200`|Query|✗|Int|

#### Response

返回的 unspent 按照确认数递减。`n_tx`字段标记了 unspent 的个数，与传入的 address 一一对应。

```JSON
{
    "unspent_outputs": [
        {
            "address": "n4eY3qiP9pi32MWC6FcJFHciSsfNiYFYgR",
            "tx_hash": "45cd71ad28c541c4ee507bac39017e89fbc028e11ebaa129c562600a71ded67f",
            "tx_index": 63000067633,
            "tx_output_n": 0,
            "script": "76a914fdb9fb622b0db8d9121475a983288a0876f4de4888ac",
            "value": 1250120000,
            "value_hex": "4a835140",
            "confirmations": 1
        }
    ],
    "n_tx": [
        61239
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
