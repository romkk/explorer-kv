Docker for Bitcoind v0.10.2
======================

## 宿主机器安装Docker

#### ubuntu 14.04 LTS

```
wget -qO- https://get.docker.com/ | sh
service docker start
service docker status
```

阿里云机器如果启动不了Docker，尝试以下命令：

```
route del -net 172.16.0.0 netmask 255.240.0.0
service docker restart
```

## 编译Docker

把docker文件`(http://gitlab.bitmain.io/dev/hayekinstallpkgs/tree/master/docker/bitcoind/v0.10.2)`全部上传到服务器，假设上传至服务器的目录为：`/root/docker/bitcoind/v0.10.2`。

非国内机器安装时，请注释掉`Dockerfile`中的：`ADD sources-163.com.list /etc/apt/sources.list`。

```
mkdir -p /root/docker/bitcoind/v0.10.2
cd /root/docker/bitcoind/v0.10.2

# build docker
docker build -t bitcoind:0.10.2 .
```

## 运行

设定宿主机服务器运行目录为： `/d1`

### 目录映射

 宿主机 | docker | 说明 
 --------|------|-----------
`/d1/bitcoind` | `/root/.bitcoin` | bitcoind数据目录
`/d1/supervise_bitcoind/bitcoind_latest_block` | `/root/supervise_bitcoind/bitcoind_latest_block` | 新块数据目录

宿主机创建相关目录，命令：

```
mkdir -p /d1/bitcoind
mkdir -p /d1/supervise_bitcoind/bitcoind_latest_block
```

docker容器启动命令：

```
docker run -it -v /d1/bitcoind:/root/.bitcoin -v /d1/supervise_bitcoind/bitcoind_latest_block:/root/supervise_bitcoind/bitcoind_latest_block --name bitcoind -p 8333:8333 -p 8332:8332 -d bitcoind:0.10.2

#
# 矿池CentOS的机器，宿主机的目录是： /opt/bitcoin/bitcoindata, /opt/hayek/supervise_udpblkrouter/latest_block。对应命令为：
#
docker run -it -v /opt/bitcoin/bitcoindata:/root/.bitcoin -v /opt/hayek/supervise_udpblkrouter/latest_block:/root/supervise_bitcoind/bitcoind_latest_block --name bitcoind -p 8333:8333 -p 8332:8332 -d bitcoind:0.10.2
```

进入容器：

```
docker exec -it bitcoind /bin/zsh
```

### bitcoin.conf 示例配置

```
rpcuser=bitcoinrpc
rpcpassword=7HFtrFBVXMy66d83xSyDDruBfbWwXLxKNeMVvn5YGRnM
rpcthreads=32

# docker 网络通常是私有网段 172.xxx.xxx.xxx，若非默认，请自行调整
rpcallowip=172.17.0.0/16

limitdownloadblocks=2016

maxconnections=768
outboundconnections=512
```
