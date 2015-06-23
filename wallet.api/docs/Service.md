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

  服务端根据 walletid 来区分用户、保存备份、推送消息等。

  以下以`wid`表示。
  
## 设备管理

###  注册

用户使用的设备需要经过注册，以提供信息推送等功能。

**Request**

```
GET /register?did=$device_id&wid=$wid
```

**Response**

```
{
    success: true
}
```

可能的错误码：

*   RegisterTooManyDevices

    `wid`对应的已注册的设备过多；当前最多不能超过 16 个。
    
*   RegisterDeviceAlreadyTaken

    该`device_id`已经对应一个`wid`。
    
### 注销

**Reqeust**

```
POST /unregister?did=$device_id&wid=$wid
```

**Response**

```
{
    success: true
}
```

## 鉴权

### 登录

1.  客户端发起登录请求，服务端返回待签名字符串。待签名字符串在 300 秒后过期，届时需要重新申请。

    **Request**

    ```
    GET /auth?device_id=$did&wid=$wid
    ```

    **Response**

    ```
    {
        "challenge": "ZGZkZmRmZA.ZGZkZmRmZA",
        "expired_at": 1434360774
    }
    ```

2.  客户端使用私钥签名，签名方式如下：

    ```
    signature = sign_by_private_key($challenge, $privateKey)
    ```

    提交认证字符串：

    **Request**

    ```
    POST /auth

    {
        "challenge": "ZGZkZmRmZA.ZGZkZmRmZA",
        "signature": "signature",
        "public_key": "my_pubkey"
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

    *   AuthDenied

        其他原因导致的服务器拒绝登录。

### 会话

在登录完成后，与服务器的会话使用`token`认证，即在 HTTP Request Header 中加入以下字段：

```
X-Wallet-Token: $token
```

会话请求到达服务器后，会首先经过鉴权模块，如果鉴权失败则有以下错误信息：

```
HTTP/1.1 401 Unauthorized

{
    "message": "Invalid Token",
    "code": "AUTH_INVALID_TOKEN"
}
```

可能的错误码：

* AuthTokenExpired

  token 过期。

* AuthInvalidToken

  token 非法。

在鉴权失败后，客户端需要重新发起登录过程。

## 交易

### 查询交易记录

请使用数据 API。

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

提交地址用于余额变动监控，每次最大 1024 个。

**Request**

```
POST /address

[
    "mzZypShcs1B35udnkqeYeJy8rUdgHDDvKG",
    "n4eY3qiP9pi32MWC6FcJFHciSsfNiYFYgR"
]
```

**Response**

成功：

```
{
    "success": true
}

```

## 多重签名账户

### 发起创建请求

**Request**

**Request**

```
POST /multi-signature-addr

```

**Response**

```
HTTP/1.1 201 CREATED

{
}
```

### 查询创建状态

**Reqeust**

```
GET /multi-signature-addr/:addr
```

### 修改创建状态

**Request**

```
PUT /multi-signature-addr/:addr
```

### 发起多重签名交易

**Request**

```
POST /multi-tx
```

**Response**

### 查询多重签名交易确认状态

**Request**

```
GET /multi-tx/:txid
```

### 修改多重签名交易确认状态

**Request**

```
PUT /multi-tx/:txid
```

## 用户数据文件的备份与恢复

基于阿里云 OSS 存储用户加密后的数据文件。

### 备份

TODO

### 恢复

TODO

## 主密钥文件的备份与恢复

基于阿里云 OSS 存储用户加密后的主密钥文件，需要使用 BM 帐号登录后方可使用。

### 备份

TODO

### 恢复

TODO

