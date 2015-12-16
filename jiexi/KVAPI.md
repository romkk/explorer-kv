# Key Value API

请求采用类似RPC的方式，两个通用参数：`method`, `params`.

### 请求

* method 为方法名称，目前可选有：
  * `ping` : 测试用途，返回 "pong" 
  * `get` : 查询键值
  * `range` : 根据起始终止键值，进行范围查询
* params 以逗号分隔的参数
  * 对所有参数进行url编码，然后用英文逗号进行拼接：`param1,param2,...,paramN`
  * 最大参数个数目前设置为：`2000`

#### get 方法
* `params` 为需要获取的键值，按照约定编码即可

#### range 方法
* `params` 有既定顺序：`start_key`, `end_key`, `limit`, `[offset=0]`
  * `offset` 可选，默认为零
  * 若字符顺序，`start_key` &lt; `end_key`，则正序获取数据
  * 若字符顺序，`start_key` &gt; `end_key`，则逆序获取数据


### 返回

以 FlatBuffers 对象进行返回，二进制格式。根节点对象为：`APIResponse`.

```
// 请以 flatbuffers/explorer.fbs 为准
table APIResponse {
  id: string;  // 默认返回 rocksdb-{timestamp,micro seconds}, eg. rocksdb-1448512810564
  error_no: int;  // 非零表示错误，零为正常
  error_msg: string;
  offset_arr: [int];     // -1表示数据不存在
  length_arr: [int];     // offset为-1的项，其length为0
  type_arr  : [string];  // key对应的类型，未知为空字符串""
  key_arr  :  [string];  // 查询的keys
  data: [ubyte];
}
```

`key_arr` 返回查询的keys，若`method`为`get`，则返回所有的keys；若`method`为`range`，则返回查询到存在的keys。

-----------------


### Examples

#### method=get

```
http://<host>:<port>/kv?method=get&params=11_0000000099c744455f58e6c6e98b671e1bf7f37346bfd4cf5d0274ad8ee660cb,01_b765754a4382b759985b746a72e9e9385caa893aa3e6cbaa55491c7487b258c2
```

#### method=get

```
http://<host>:<port>/kv?method=range&params=21_151HTYUr2edVqzUa1w3sPFDAMZAFuXJ1Gy_0000000003,21_151HTYUr2edVqzUa1w3sPFDAMZAFuXJ1Gy_0000000000,10
```

#### method=range

```
http://<host>:<port>/kv?method=range&params=14_9999999999,14_0000000000,5,0
http://<host>:<port>/kv?method=range&params=14_0000000000,14_9999999999,5,20
```
