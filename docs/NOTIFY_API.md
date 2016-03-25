使用事件通知
==========

目前事件通知有两种类型：`块`, `交易`。

数据库中的存储数据示例：

```
id,type,hash,height,address,amount,created_at
1326,10,00000000000002c87748cddeba7d5c7f22c8577782de752faa551655c4f4777a,725226,"",0,2016-03-15 07:15:00
1327,21,7fb1bc31b9125977210eaed45310d54481cf24d5e1cbfb7a46f8f26db7ab8c8a,725226,mqkweWc4RYWxAH653yBe1XNsNmjRp5g1Z4,629034123,2016-03-15 07:15:00
1328,20,c18298bee222c024be61027392abf8cd1227bb5e1f13d1ba6ec88a15d838b407,-1,mqkweWc4RYWxAH653yBe1XNsNmjRp5g1Z4,625000000,2016-03-15 07:15:10
```


如何使用
==========


## 订阅/退订

URL：

```
http://<host>:<port>/address?token=<token>&appid=<app_id>&method=<method>&address=<address>
```

参数说明：

* token: 固定配置，根据不同环境，由服务端配置文件完成，调用时填入其相应值即可
* method： 可选值为`insert`, `delete`。
* appid: 每个应用，自行设置自己的APP ID。
* address：需要订阅/退订的地址

订阅示例：

```
http://127.1:7900/address?token=TESTNET3_DEV_pOE9Sx8&appid=2&method=insert&address=myysrNPo3CV4RbfZrvipaRbbRade8QkWBo
```

退订示例：

```
http://127.1:7900/address?token=TESTNET3_DEV_pOE9Sx8&appid=2&method=delete&address=myysrNPo3CV4RbfZrvipaRbbRade8QkWBo
```

操作成功后返回：

```
{
   "error_msg" : "",
   "error_no" : 0
}
```

失败则返回响应的错误，示例：

```
{
   "error_no" : 20,
   "error_msg" : "address already exist"
}
```

## 获取消息

订阅成功后，会收到两类消息，块通知和交易通知。只要APP ID存在，默认均会收到块通知；但交易类型的只会收到订阅相关地址的。

应用方应该自行保存其游标，用于下次获取时使用。

拉取通知消息URL：

```
http://<host>:<port>/pullevents/?token=<token>&appid=<app_id>&offset=<offset>
```

`offset`为上次获取的最大ID值，本次结果将显示大于此值的数据。目前每次最大返回`1000 `条数据。

示例：

```
http://127.1:7900/pullevents?token=TESTNET3_DEV_pOE9Sx8&appid=1&offset=1374
```

返回：

```
{
   "error_no" : 0,
   "error_msg" : "",
   "results" : [
      {
         "id" : 2282,
         "address" : "",
         "amount" : 0,
         "height" : 725261,
         "hash" : "000000000000026918dd29f046482dc36b9c458322caec7ffbf9f528d7768aef",
         "type" : 10,
         "created_at" : "2016-03-15 10:13:44"
      },
      {
         "id" : 2283,
         "address" : "",
         "amount" : 0,
         "hash" : "0000000000000f0892d1b0768136f6d430148ba64a76a71c3a5f697f54f1d2bd",
         "height" : 725262,
         "type" : 10,
         "created_at" : "2016-03-15 10:21:40"
      },
      {
         "address" : "",
         "id" : 2284,
         "created_at" : "2016-03-15 10:26:19",
         "type" : 10,
         "hash" : "00000000000007857608c89e7057cfcb5e4a33b5ee56d6307217dc4671ea03bc",
         "height" : 725263,
         "amount" : 0
      }
   ]
}
```

`offset`之后没有数据，则返回：

```
{
   "error_no" : 30,
   "error_msg" : "app events table is empty"
}
```


APP ID分配表
===========

app id | 使用方
-------|--------
1 | btc.com web，目前有邮件订阅


