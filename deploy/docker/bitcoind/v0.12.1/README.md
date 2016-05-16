Docker for Bitcoind v0.12.1
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

把docker文件`(http://gitlab.bitmain.io/dev/hayekinstallpkgs/tree/master/docker/bitcoind/v0.12.1)`全部上传到服务器，假设上传至服务器的目录为：`/root/docker/bitcoind/v0.12.1`。

非国内机器安装时，请注释掉`Dockerfile`中关于`/etc/apt/sources.list`的配置（国内做了加速，使用国内的源）。

```
mkdir -p /work/docker/bitcoind/v0.12.1
cd /work/docker/bitcoind/v0.12.1

# build docker
docker build -t explorer-bitcoind:0.12.1 .
# docker build --no-cache -t explorer-bitcoind:0.12.1 .
```

## 运行

设定宿主机服务器运行目录为： `/work`

### 目录映射

 宿主机 | docker | 说明 
 --------|------|-----------
`/work/bitcoind` | `/root/.bitcoin` | bitcoind数据目录

宿主机创建相关目录，命令：

```
mkdir -p /work/bitcoind
```

docker容器启动命令：

```
docker run -it -v /work/bitcoind:/root/.bitcoin --name explorer-bitcoind -p 8333:8333 -p 8332:8332 -p 18333:18333 -p 18332:18332 --restart always -d explorer-bitcoind:0.12.1
```

进入容器：

```
docker exec -it explorer-bitcoind /bin/zsh
```

### bitcoin.conf 示例配置

```
rpcuser=bitcoinrpc
rpcpassword=7HFtrFBVXMy66d83xSyDDruBfbWwXLxKNeMVvn5YGRnM
rpcthreads=32

# docker 网络通常是私有网段 172.xxx.xxx.xxx，若非默认，请自行调整
rpcallowip=172.17.0.0/16
rpcallowip=192.168.0.0/16
rpcallowip=10.0.0.0/24
```

参数项： `customlogdir`，请勿放在 `bitcoin.conf` 中，会导致 main 和 testnet3 写同一个目录，尽量放到 bitcoind 的启动命令里:

```
bitcoind ... -customlogdir=/root/.bitcoin/customlog
bitcoind ... -customlogdir=/root/.bitcoin/testnet3/customlog
```
