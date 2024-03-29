bitcoind-v0.12.1
================


关于内存池部分，v0.12.x最大的改进是内存池可进可出，之前是只进不出，现在支持“过期”和“裁剪”操作，这两个操作均会从内存池移除交易，故新增交易移除的日志类型。

详情参考bitcoind对应的patch.

## 移除

递归移除表示，不仅移除当前交易，还会移除此交易的所有已花费子孙交易。

### 输出日志
* 时间过期。移除时间过老的交易。（递归移除）
* 体积过大。则按照交易优先级排序，移除低优先级的交易。（递归移除）

### 忽略日志
* 块确认。移除本块中被确认的交易。**不输出日志**，由`log2producer`根据新块交易自行移除其内存池的交易。在bitcoind中，会先输出移除交易日志（且是不递归移除），再输出新块日志。因为`log2producer`处理移除交易日志时是递归型的，所以会出现子孙交易一并被移除的问题。目前在bitcoind中，当不需要递归移除时，说明是新块到来。

## 新增
### 输出日志
* 新交易。从网络上接收的新的交易。

### 忽略日志
* 块回退。当回退一个块时，块中的交易会重新放入bitcoind内存池，**不输出日志**，由`log2producer`根据回退块中的交易自行放入内存池。因bitcoind先输出交易日志，再输出块日志，若输出的话，当`log2producer`处理交易日志时，其实会导致一笔交易被接收两次。由`log2producer`自己处理，则仅发生状态变化从`confirmed`变为`unconfirm`。