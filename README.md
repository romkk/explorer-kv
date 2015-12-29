# Explorer

## Flat Buffers

并不需要依赖其库，仅需要头文件即可。下面是安装步骤备忘，本地生成头文件需要：

```
wget https://github.com/google/flatbuffers/archive/v1.2.0.tar.gz -O flatbuffers-1.2.0.tar.gz
tar zxf flatbuffers-1.2.0.tar.gz
cd flatbuffers-1.2.0
cmake -G "Unix Makefiles"
make
make install
```

## Amazon - EC2

部署需要跳高 ulimit 设置，默认1024远远不够使用。 `/etc/security/limits.conf` 末尾添加：

```
 root soft nofile 65535
 root hard nofile 65535
 * soft nofile 65535
 * hard nofile 65535
```
然后重启系统以生效。

## Amazon - RDS

调大部分Mysql参数：

   Parameter | default.mysql5.6 | explorer-mysql-5-6
-------------|------------------|--------------------
innodb\_log\_file\_size | 134217728 | 1048576000
max\_allowed\_packet | &lt;engine-default&gt; | 67108864
tmp\_table\_size | &lt;engine-default&gt; | 536870912
max\_heap\_table\_size | &lt;engine-default&gt; | 536870912