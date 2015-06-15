# Wallet Development Reference

## 说明

* 在 HTTP 请求的 header 中务必加入以下字段，开启响应内容的 Gzip 压缩，有效节省客户端流量：

  ```
  Accept-Encoding: gzip;
  ```
  
* 提交和返回的内容均为 JSON 格式；

## 概念

* Wallet_ID
  
  钱包的全局唯一 id，生成方式如下：
  
    ```
    'w_' + sha256(sha256($private_key))
    ```
        
  服务端根据 walletid 来保存备份、区分用户、推送消息等。
  
  以下简称为 `wid`。
  
### 鉴权

#### 登录

1.  客户端发起登录请求，服务端返回待签名字符串。待签名字符串在 300 秒后过期，届时需要重新申请。

    **Request**
    
    ```
    GET /auth
    ```
    
    **Response**
    
    ```
    {
        "challenge": "ZGZkZmRmZA",
        "expiredAt": 1434360774
    }
    ```
    
2.  客户端使用私钥签名，签名方式如下：

    ```
    signature = signByPrivateKey($challenge + $expiredAt + $wid)
    ```

    提交认证字符串：

    **Request**
    
    ```
    POST /auth/verify
    
    {
        "challenge": "ZGZkZmRmZA",
        "signature": "signature",
        "publicKey": "my_pubkey",
        "wid": "mywid"
    }
    ```
    
    **Response**
    
    成功：
    
    ```
    HTTP/1.1 200 OK
    
    {
        "success": true,
        "token": "yourtoken",
        "expiredAt": "1434360614"
    }
    ```
    
    失败：
    
    ```
    HTTP/1.1 401 Unauthorized
    
    {
        "success": false,
        "message": "Invalid Signature",
        "code": "AUTH_INVALID_SIGNATURE"
    }
    ```
    
    可能的错误码：
    
    * AUTH\_INVALID\_SIGNATURE
      
      无效签名。
      
    * AUTH\_INVALID\_CHALLENGE

      待签名字符串过期或不存在。
      
    * AUTH\_DENIED

      其他原因导致的服务器拒绝登录。
      
#### 登录成功后处理

读取或新建`wid`的账户信息，包括：

1. 在 OSS 上新建备份存储目录；
2. 
    
#### 会话

在登录完成后，与服务器的会话使用`token`认证，即在 HTTP Request Header 中加入以下字段：

```
X-BMW-Token: $token
```

请求到达服务器后，会首先经过鉴权模块，如果鉴权失败则有以下错误信息：

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

### 普通交易

#### 发起交易

#### 查询交易  

### 多重签名账户

#### 发起创建请求

#### 查询创建状态

#### 修改创建状态

### TBD

