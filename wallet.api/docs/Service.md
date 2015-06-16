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

  服务端根据 walletid 来保存备份、区分用户、推送消息等。

  以下以`wid`表示。

## 鉴权

### 登录

1.  客户端发起登录请求，服务端返回待签名字符串。待签名字符串在 300 秒后过期，届时需要重新申请。

    **Request**

    ```
    GET /auth
    ```

    **Response**

    ```
    challenge = sha256($request_ip + $random)
    ```

    ```
    {
        "challenge": "ZGZkZmRmZA",
        "expired_at": 1434360774
    }
    ```

2.  客户端使用私钥签名，签名方式如下：

    ```
    signature = sign_by_private_key($challenge + $expired_at + $wid)
    ```

    提交认证字符串：

    **Request**

    ```
    POST /auth

    {
        "challenge": "ZGZkZmRmZA",
        "signature": "signature",
        "public_key": "my_pubkey",
        "wid": "mywid"
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

    * AUTH\_INVALID\_SIGNATURE

      无效签名。

    * AUTH\_INVALID\_CHALLENGE

      待签名字符串过期或不存在。

    * AUTH\_DENIED

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

* AUTH\_TOKEN\_EXPIRED

  token 过期。

* AUTH\_INVALID\_TOKEN

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
            "tx_hash": "txhash",
            "tx_hash_big_endian": "shhatx",
            "script": "script",
            "tx_output_n": 0,
            "value": 12345
        },
        {
            "tx_hash": "txhash",
            "tx_hash_big_endian": "shhatx",
            "script": "script",
            "tx_output_n": 1,
            "value": 45678
        }
    ]
}
```

可能的错误码：

* TX_UNAFFORDABLE

  余额不足。

* TX_FAILED

  其他原因导致的失败。

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

* TX\_PUBLISH\_INVALID_HEX

  hex 非法。

* TX\_PUBLISH\_FAILED

  其他原因导致的发布失败。

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

可能的错误码：

* ADDRESS\_WATCH\_FAILED

  添加监控失败。

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

