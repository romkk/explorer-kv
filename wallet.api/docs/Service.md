# Wallet Development Reference

## 说明

* 在 HTTP 请求的 header 中务必加入以下字段，开启响应内容的 Gzip 压缩，有效节省客户端流量：

  ```
  Accept-Encoding: gzip;
  ```

* 提交和返回的内容均为 JSON 格式；
* 所有的货币单位均为 satoshi；

## 概念

* Wallet_ID

  钱包的全局唯一 id，生成方式如下：

  ```
  'w_' + sha256(sha256($private_key))
  ```

  服务端根据 walletid 来区分用户、保存备份、推送消息等，以下以`wid`表示。
  
## 鉴权

### 登录

1.  客户端发起登录请求，服务端返回待签名字符串。待签名字符串在 300 秒后过期，届时需要重新申请。

    **Request**

    ```
    GET /auth/$wid
    ```

    **Response**

    ```
    {
        "address": "mtJL8KeTugcf2YCqvxbFatUbNYDywBFfNR",    -- 如果不是初次认证，则需要使用该地址进行签名；否则用任意地址签名即可
        "challenge": "eyJ3aWQiOiJ3XzJiMWVkZDI1Mzc0MDMzOWM1MWNiYTBkNGQ4NmM3MjNiYjRkMWNiMWNmMzA4ZTE5NzUxODk1ODQ3MzYzNTI2M2QiLCJleHBpcmVkX2F0IjoxNDM1MjE2MTI0LCJub25jZSI6IjUxMjlmNWJiOTIiLCJhZGRyZXNzIjoibXRKTDhLZVR1Z2NmMllDcXZ4YkZhdFViTllEeXdCRmZOUiJ9.c4M9WxmypJQB8pApexRLeQG6kiu1Hdav7G/UzxYIxqE",
        "expired_at": 1435216124
    }
    ```

2.  客户端使用私钥签名，签名方式如下：

    ```
    signature = sign_by_private_key($challenge, $privateKey)
    ```

    提交认证字符串：

    **Request**

    ```
    POST /auth/$wid

    {
        "address": "mtJL8KeTugcf2YCqvxbFatUbNYDywBFfNR",    -- 如果不是首次认证，需要与指定的地址一致；否则传入任意地址，该 wid 将于该地址绑定
        "challenge": "eyJ3aWQiOiJ3XzJiMWVkZDI1Mzc0MDMzOWM1MWNiYTBkNGQ4NmM3MjNiYjRkMWNiMWNmMzA4ZTE5NzUxODk1ODQ3MzYzNTI2M2QiLCJleHBpcmVkX2F0IjoxNDM1MjA1ODI3LCJub25jZSI6IjA0MDAxMTE0OTciLCJhZGRyZXNzIjoibXRKTDhLZVR1Z2NmMllDcXZ4YkZhdFViTllEeXdCRmZOUiJ9.4qgCCmDnb03BtA2tMXC9+LJWIgSGGKi2/gM0gtH41+I",
        "signature": "H5YB9+qSvk1MU3FWrt72VI1qB7MvnRk8NVaIpCeFP2vIAWVdSz99In40o3yJFWY/fTR458xWy8110QmCnjWRjJA=",
        "device": {
            "id": "deviceid",
            "os": "iOS",      -- enum('iOS','Android','Windows','Other')
            "version": "9.0",
            "lang": "zh-CN"        -- enum('en-US','zh-CN') http://www.lingoes.net/en/translator/langcode.htm
        }
    }
    ```

    **Response**

    成功：

    ```
    {
        "success": true,
        "token": "yourtoken",
        "expired_at": "1434360614"
    }
    ```

    可能的错误码：

    *   AuthInvalidSignature

        无效签名。

    *   AuthInvalidChallenge

        待签名字符串过期或不存在。
        
    *   AuthNeedBindAddress
        
        待签名字符串已经被其他请求通过验证并处理，客户端重试认证流程即可。
        
    *   RegisterTooManyDevices
    
        `wid`对应的已注册的设备过多；当前最多不能超过 10 个。
        
    *   RegisterDeviceAlreadyTaken
    
        该`device_id`已经对应一个`wid`。
        
    *   AuthDenied

        其他原因导致的服务器拒绝登录。

### 会话

在登录完成后，与服务器的会话使用`token`认证。需在 HTTP Request Header 中加入以下字段：

```
X-Wallet-Token: $token
```

会话请求到达服务器后，会首先经过鉴权模块，如果鉴权失败则有以下错误信息：

```
HTTP/1.1 401 Unauthorized

{
    "message": "Invalid Token",
    "code": "UnauthorizedError"
}
```

### 跳过鉴权

开发时可以跳过鉴权，在 url 中加入`skipauth=1`即可。

## 设备管理

### 注册

设备注册在获取 token 的过程中完成。
    
### 注销

TODO

## 普通交易

### 查询交易记录

**Request**

```
GET /tx
```

区块数据 API 的简单封装，使用`active`、`offset`、`limit`、`timestamp`和`sort`参数，具体参考区块数据 API。

**Response**

```
[
    {
        "amount": -100010000,
        "confirmations": 23132,
        "inputs": [
            "mtJL8KeTugcf2YCqvxbFatUbNYDywBFfNR"
        ],
        "outputs": [
            "mxvXieXAHNHJS6xG1YgKgb2VQhfVrPgjZ5",
            "mtJL8KeTugcf2YCqvxbFatUbNYDywBFfNR"
        ],
        "time": 1435317403,
        "txhash": "14655dca122f353c16c547a033d2d1c4129492395eedd90bfc4390ad28cb4bc3"
    }
]
```

### 查询交易详细信息

请使用数据 API。

### 提交构造交易请求

**Request**

```
POST /tx

{
    "fee_per_kb": 10000,
    "from": [ "mqyAnt6z7rSqqRvmKQSWqdiesAgFEVWegT" ],
    "to": [
        {
            "addr": "n4eY3qiP9pi32MWC6FcJFHciSsfNiYFYgR",
            "amount": 12345
        },
        {
            "addr": "mzZypShcs1B35udnkqeYeJy8rUdgHDDvKG",
            "amount": 45678
        }
    ]
}
```

**Response**

成功：

```
{
    "success": true,
    "fee": 10000,
    "unspent_txs": [
        {
            "address": "n4eY3qiP9pi32MWC6FcJFHciSsfNiYFYgR",
            "confirmations": 173450,
            "script": "76a914b96b816f378babb1fe585b7be7a2cd16eb99b3e488ac",
            "tx_hash": "c1b01e96d2aa9b6a53abe6d5301e92144792fa8ddbcf7b3cd41b907b7c1f242d",
            "tx_index": 45000043241,
            "tx_output_n": 0,
            "value": 7,
            "value_hex": "7"
        },
        {
            "address": "mzZypShcs1B35udnkqeYeJy8rUdgHDDvKG",
            "confirmations": 173136,
            "script": "76a914b96b816f378babb1fe585b7be7a2cd16eb99b3e488ac",
            "tx_hash": "ec35d4e66b94bbbafb848658bdc4f6afb003ba4205b779991c7614659112c690",
            "tx_index": 16000043299,
            "tx_output_n": 0,
            "value": 7,
            "value_hex": "7"
        }
    ]
}
```

可能的错误码：

* TxUnaffordable

  余额不足。

### 广播交易

**Request**

```
POST /tx/publish

{
    "hex": "HEXSTRING"
}
```

**Response**

```
{
    "success": true
}
```

可能的错误码：

* TxPublishFailed

  发布失败，详细信息请关注`bitcoind`字段，含有调用 bitcoind 返回的错误信息。

### 监控地址余额变动

TODO

## 多重签名账户

### 发起创建请求

**Request**

```
POST /multi-signature-account

{
    "account_name": "Bitmain",
    "creator_name": "hammer",
    "creator_pubkey": "0491bba2510912a5bd37da1fb5b1673010e43d2c6d812c514e91bfa9f2eb129e1c183329db55bd868e209aac2fbc02cb33d98fe74bf23f0c235d6126b1d8334f86",
    "m": 3,
    "n": 2
}
```

**Response**

```
{
    "account_name": "Bitmain",
    "complete": false,
    "generated_address": null,
    "id": 17,
    "m": 2,
    "n": 1,
    "participants": [
        {
            "is_creator": true,
            "joined_at": 1435690258,
            "name": "hammer",
            "pos": 0,
            "pubkey": "0491bba2510912a5bd37da1fb5b1673010e43d2c6d812c514e91bfa9f2eb129e1c183329db55bd868e209aac2fbc02cb33d98fe74bf23f0c235d6126b1d8334f86"
        }
    ],
    "redeem_script": null,
    "success": true
}
```

可能的错误码：

*   MultiSignatureAccountInvalidParticipantCount

    指定的 M/N 无效。

*   MultiSignatureAccountInvalidPubkey

    指定的公钥无效，已被使用过或者非法。

*   MultiSignatureAccountCreateFailed

    多重签名账户创建失败，可能是竞态条件冲突等，重试即可。

### 查询创建状态

**Reqeust**

```
GET /multi-signature-account/$id
```

**Response**

```
{
    "success": true,
    "complete": false,
    "generated_address": null,      -- 当 complete 为 true 时，此字段标记了生成的地址
    "redeem_script": null,          -- 同上
    "id": 11,
    "m": 1,
    "n": 1,
    "participant": [
        {
            "is_creator": true,             -- 接盘身份
            "joined_at": 1435677824,        -- 接盘时间
            "name": "haha",                 -- 接盘客名称
            "pos": 0,                       -- 生成地址时传入的顺序
            "pubkey": "04865c40293a680cb9c020e7b1e106d8c1916d3cef99aa431a56d253e69256dac09ef122b1a986818a7cb624532f062c1d1f8722084861c5c3291ccffef4ec6874"
        }
    ]
}
```

如果查询的账户不存在，则返回 404。

### 修改创建状态

**Request**

```
PUT /multi-signature-account/$id

{
    "name": "second",
    "pubkey": "048d2455d2403e08708fc1f556002f1b6cd83f992d085097f9974ab08a28838f07896fbab08f39495e15fa6fad6edbfb1e754e35fa1c7844c41f322a1863d46213" 
}
```

**Response**

```
{
    "complete": true,
    "generated_address": "2N2NGkKeCEuAAfeDeHuDd4SRfFUNvv7LQDS",
    "id": 12,
    "m": 2,
    "n": 2,
    "participants": [
        {
            "is_creator": true,
            "joined_at": 1435683669,
            "name": "haha",
            "pos": 0,
            "pubkey": "04865c40293a680cb9c020e7b1e106d8c1916d3cef99aa431a56d253e69256dac09ef122b1a986818a7cb624532f062c1d1f8722084861c5c3291ccffef4ec6874"
        },
        {
            "is_creator": false,
            "joined_at": 1435684088,
            "name": "second",
            "pos": 1,
            "pubkey": "048d2455d2403e08708fc1f556002f1b6cd83f992d085097f9974ab08a28838f07896fbab08f39495e15fa6fad6edbfb1e754e35fa1c7844c41f322a1863d46213"
        }
    ],
    "redeem_script": "524104865c40293a680cb9c020e7b1e106d8c1916d3cef99aa431a56d253e69256dac09ef122b1a986818a7cb624532f062c1d1f8722084861c5c3291ccffef4ec687441048d2455d2403e08708fc1f556002f1b6cd83f992d085097f9974ab08a28838f07896fbab08f39495e15fa6fad6edbfb1e754e35fa1c7844c41f322a1863d4621352ae",
    "success": true
}
```

可能的错误码：

*   MultiSignatureAccountInvalidParams

    公钥、用户名或 wid 冲突。正常情况下客户端请求用户重新指定名称即可，公钥和 wid 的冲突避免由客户端保证。

*   MultiSignatureAccountJoinCompleted

    访问时该多重签名账户已完成。

*   MultiSignatureAccountJoinFailed

    多重签名账户更新失败，可能是竞态条件冲突等，重试即可；
    
    如果含有 description 字段，则为 bitcoind 返回错误；注意：为了维护状态一致，该账户会被删除，需要重复创建流程。
    
如果要加入的账户不存在，则返回 404。

### 取消创建

只能取消尚未生成（`complete = false`）的多重签名账户。
 
**Request**

```
DELETE /multi-signature-account/$id
```

**Response**

```
{
    success: true
}
```

可能的错误码：

*   MultiSignatureAccountCreated

    要删除的多重签名账户已经生成，不可被删除
    
*   MultiSignatureAccountDenied

    要删除的多重签名账户并非请求者创建，不可被删除
    
*   MultiSignatureAccountDeleteFailed

    删除失败，一般是由于竞态条件，重试即可
    
如果要删除的账户不存在，则返回 404。

## 多重签名交易

### 获取最新的未完成多重签名交易

**Request**

```
GET /multi-signature-account/$account_id/tx/latestUnfinished
```

**Response**

返回值与多重签名交易详情相同。如果该交易不存在，则返回 404。

### 获取多重签名交易列表

**Request**

```
GET /multi-signature-account/$account_id/tx
```

支持用于分页的`offset`、`limit`参数。

**Response**

```
[
    {
        "amount": -100010000,
        "id": 57,
        "inputs": [
            "mtJL8KeTugcf2YCqvxbFatUbNYDywBFfNR"
        ],
        "note": "",
        "outputs": [
            "mpZM35ZsEFQ7djDZL4UskcGmrVebsHAm76",
            "mtJL8KeTugcf2YCqvxbFatUbNYDywBFfNR"
        ],
        "status": "TBD",
        "timestamp": 1435313711,
        "txhash": "dbf23dc6101cafacaf3acec615864b4a82be7fe71802ff0bc19bd0ea01a6e784",
        "is_deleted": false,
        "deleted_at": -1
    }
]
```

### 发起多重签名交易

**Request**

```
POST /multi-signature-account/$account_id/tx

{
    "rawtx": "0100000001aca7f3b45654c230e0886a57fb988c3044ef5e8f7f39726d305c61d5e818903c00000000fd15010048304502200187af928e9d155c4b1ac9c1c9118153239aba76774f775d7c1f9c3e106ff33c0221008822b0f658edec22274d0b6ae9de10ebf2da06b1bbdaaba4e50eb078f39e3d78014cc952410491bba2510912a5bd37da1fb5b1673010e43d2c6d812c514e91bfa9f2eb129e1c183329db55bd868e209aac2fbc02cb33d98fe74bf23f0c235d6126b1d8334f864104865c40293a680cb9c020e7b1e106d8c1916d3cef99aa431a56d253e69256dac09ef122b1a986818a7cb624532f062c1d1f8722084861c5c3291ccffef4ec687441048d2455d2403e08708fc1f556002f1b6cd83f992d085097f9974ab08a28838f07896fbab08f39495e15fa6fad6edbfb1e754e35fa1c7844c41f322a1863d4621353aeffffffff0140420f00000000001976a914ae56b4db13554d321c402db3961187aed1bbed5b88ac00000000",
    "note": "i'm rich"，
    "complete": false        -- 当前是否签名完成，一般为 false
}
```

**Response**

```
{
    "success": true,
    ...
}
```

可能的错误码：

*   MultiSignatureTxUnfinishedExists

    账户下有尚未完成的交易

*   MultiSignatureTxInvalidHex

    hex 非法
    
*   MultiSignatureTxCreateFailed

    创建失败

如果指定的账户不存在，则返回 404。

### 查询多重签名交易确认状态

**Request**

```
GET /multi-signature-account/$account_id/tx/$tx_id
```

**Response**

```
{
    "created_at": 1437485520,
    "deleted_at": -1,
    "hex": "0100000001431bafdd86da89dc2d531d13491c7fa69cda62de7e17abb4b75421826d13c483000000006b483045022100b03879e692b498231f0c61b62fef613d7b9427ee0bb3442bd25f595ec354bb3502205c2ab8e9150bc00584c7c98eba0fac62bcedcf294ee26e5b670cb0b6831b5b2a012102c2ce65118ca9d9c39ce8e71edc39d17d9800ae3c0089931cb5e7e4a5ac98ff7bffffffff0200e1f505000000001976a914beef9b3e54bbc0405cd45cd6fbe97c8b596480d088accaf80c8f000000001976a9148c3673c9d3744197ea3c3f90d5b6437d393764ff88ac00000000",
    "id": 56,
    "is_deleted": true,
    "multisig_account_id": 41,
    "note": "i'm rich",
    "participants": [
        {
            "is_creator": false,
            "joined_at": 1437151671,
            "participant_name": "first",
            "seq": 0,
            "status": "TBD"
        },
        {
            "is_creator": false,
            "joined_at": 1437151671,
            "participant_name": "second",
            "seq": 1,
            "status": "DENIED"
        },
        {
            "is_creator": true,
            "joined_at": 1437151671,
            "participant_name": "third",
            "seq": 2,
            "status": "APPROVED"
        }
    ],
    "status": "TBD",
    "success": true,
    "txhash": "14655dca122f353c16c547a033d2d1c4129492395eedd90bfc4390ad28cb4bc3",
    "updated_at": 1437123744
}
```

如果指定的账户不存在，则返回 404。

### 修改多重签名交易确认状态

**Request**

```
PUT /multi-signature-account/$account_id/tx/$tx_id

{
    "status": "DENIED",      -- enum: ["APPROVED", "DENIED"]
    "original": "",     -- 签名前 tx rawhex
    "complete" false,   -- 多重签名是否完成，如果拒绝交易，则为可选字段
    "signed": ""       -- 签名后 tx rawhex，如果拒绝交易，则为可选字段
}
```

**Response**

```
{
    "created_at": 1437064638,
    "hex": "0100000001aca7f3b45654c230e0886a57fb988c3044ef5e8f7f39726d305c61d5e818903c00000000fd5d010048304502200187af928e9d155c4b1ac9c1c9118153239aba76774f775d7c1f9c3e106ff33c0221008822b0f658edec22274d0b6ae9de10ebf2da06b1bbdaaba4e50eb078f39e3d78014730440220795f0f4f5941a77ae032ecb9e33753788d7eb5cb0c78d805575d6b00a1d9bfed02203e1f4ad9332d1416ae01e27038e945bc9db59c732728a383a6f1ed2fb99da7a4014cc952410491bba2510912a5bd37da1fb5b1673010e43d2c6d812c514e91bfa9f2eb129e1c183329db55bd868e209aac2fbc02cb33d98fe74bf23f0c235d6126b1d8334f864104865c40293a680cb9c020e7b1e106d8c1916d3cef99aa431a56d253e69256dac09ef122b1a986818a7cb624532f062c1d1f8722084861c5c3291ccffef4ec687441048d2455d2403e08708fc1f556002f1b6cd83f992d085097f9974ab08a28838f07896fbab08f39495e15fa6fad6edbfb1e754e35fa1c7844c41f322a1863d4621353aeffffffff0140420f00000000001976a914ae56b4db13554d321c402db3961187aed1bbed5b88ac00000000",
    "id": 54,
    "is_deleted": false,
    "multisig_account_id": 40,
    "note": "imrich",
    "participants": [
        {
            "joined_at": 1437064638,
            "participant_name": "hammer",
            "seq": 0,
            "status": "APPROVED"
        },
        {
            "joined_at": 1437064638,
            "participant_name": "second",
            "seq": 1,
            "status": "DENIED"
        },
        {
            "joined_at": 1437064638,
            "participant_name": "third",
            "seq": 2,
            "status": "DENIED"
        }
    ],
    "status": "DENIED",
    "success": true,
    "updated_at": 1437036796
}
```

可能的错误码：

*   MultiSignatureTxInvalidParams

    参数错误；如审批状态为批准，但是没有`signed`等字符串。

*   MultiSignatureTxCreated

    多重签名交易已经完成，不能再修改。

*   MultiSignatureTxDeleted

    多重签名交易已经取消，不能再修改。
    
*   MultiSignatureTxHexDismatch

    `original`字段与服务端的`hex`不一致。

### 取消多重签名交易

**Request**

```
DELETE /multi-signature-account/$account_id/tx/$tx_id
```

**Response**

```
{
    success: true
}
```

如果指定的账户不存在，则返回 404。

可能的错误码：

*   MultiSignatureTxCreated

    多重签名交易已经完成，不可删除。
    
*   MultiSignatureDeleteFailed

    删除失败，重试即可。
    
## 获取时间

**Request**

```
GET /timestamp
```

**Response**

```
{
    "success": true,
    "timestamp": 1438053162
}
```

## 绑定 BM 账户

用户在登录 BM 账户后，需要将`wid`与`BM`账户建立关联关系，便于恢复主密钥时使用。

当前暂不提供解绑接口，同时限定`BM`账户只能绑定一个`wid`。

**Request**

```
POST /bm-account/bind

{
    "bm_account": "myaccount"
}
```

**Response**

```
{
    "success": true,
    "account": "myaccount",
    "wids": [
        "wid1"      -- 当前是一一对应关系，将来可能会有多个
    ]
}

```

可能的错误码：

  * BMAccountUsed

    该 BM 账户已经与某个`wid`绑定。
    
  * BMAccountBindFailed

    绑定失败

## 用户数据文件的备份与恢复

基于阿里云 OSS 存储用户加密后的数据文件。客户端程序首先到钱包服务器请求临时密钥，该密钥用于访问读写阿里云的 OSS。

在取得临时密钥后，客户端直接与 OSS 进行通信，进行文件存取。

默认客户端的存储路径为 `/wallet-user-data/$wid/*`。

注意，在使用`GetBucket`接口获取 object list 时，如果 prefix 只指定了`wid`，末尾的`/`不可忽略。

iOS SDK：[http://docs.aliyun.com/#/pub/oss/sdk/ios-sdk&preface](http://docs.aliyun.com/#/pub/oss/sdk/ios-sdk&preface)

Android SDK：[http://docs.aliyun.com/#/pub/oss/sdk/android-sdk&preface](http://docs.aliyun.com/#/pub/oss/sdk/android-sdk&preface)

**Request**

```
GET /sts-token
```

**Response**

```
{
    "Credentials": {
        "AccessKeyId": "STS.DXVA15L3saBuHaLfNRyJ",
        "AccessKeySecret": "2bIDcudxSmX1ePRUaB5KS2i1AxPdurHgsWoAn6DF",
        "Expiration": "2015-07-27T09:40:32.617Z",
        "SecurityToken": "CAES8AIIARKAAQ/q4dxXHYiac8a+JraoHwTmJY2/szROv9jjp57LkY1iHYbXOy/iRvD/0sw5fRAqdBaXN16sg4iwiOJbB1BM/hR+KSoPap1TJJdZtZk9KBiThRUMZ2uvn4TBaMDIq7XlqKRLjQn7XY13oiBsg5ITivSrOKysMYp6qWWus5QKnqcSGhhTVFMuRFhWQTE1TDNzYUJ1SGFMZk5SeUoiEDE1ODExMDg2OTMxMzEwODcqBG51bGww6amK9+wpOgZSc2FNRDVCpwEKATEaoQEKBUFsbG93EkYKDEFjdGlvbkVxdWFscxIGQWN0aW9uGi4KDW9zczpQdXRPYmplY3QKDW9zczpHZXRPYmplY3QKDm9zczpIZWFkT2JqZWN0ElAKDlJlc291cmNlRXF1YWxzEghSZXNvdXJjZRo0CjJhY3M6b3NzOio6MTU4MTEwODY5MzEzMTA4Nzp3YWxsZXQtdXNlci1kYXRhL251bGwvKg=="
    },
    "FederatedUser": {
        "Arn": "acs:sts::1581108693131087:federated-user/null",
        "FederatedUserId": "1581108693131087:null"
    },
    "success": true
}
```
