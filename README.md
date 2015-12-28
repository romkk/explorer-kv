# Explorer

## Flat Buffers

并不需要依赖其库，仅需要头文件即可。下面是安装步骤备忘：

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