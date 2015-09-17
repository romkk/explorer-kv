#
# Dockerfile
#
# @author zhibiao.pan@bitmain.com
# @copyright Bitmain.com
# @since 2015-09-17
#
#
FROM phusion/baseimage:0.9.16
MAINTAINER PanZhibiao <zhibiao.pan@bitmain.com>

ENV HOME /root
ENV TERM xterm
CMD ["/sbin/my_init"]

# update apt
#ADD sources-163.com.list    /etc/apt/sources.list
#ADD sources-aliyun.com.list /etc/apt/sources.list
RUN apt-get update
RUN apt-get install -y wget zsh git build-essential autotools-dev libtool autoconf automake pkg-config daemontools sysv-rc-conf ntp nload unzip
RUN apt-get install -y libssl-dev libdb++-dev libdb-dev libboost-all-dev
RUN apt-get install -y bsdmainutils
RUN mkdir -p /root/source

# oh my zsh
ADD oh-my-zsh.tar.gz /root/oh-my-zsh.tar.gz
RUN cp -r /root/oh-my-zsh.tar.gz/.oh-my-zsh ~/.oh-my-zsh \
  && cp ~/.oh-my-zsh/templates/zshrc.zsh-template ~/.zshrc \
  && chsh -s /bin/zsh

# bitcoind
ADD v0.10.2.tar.gz /root/source/v0.10.2.tar.gz
ADD bitcoind-hubnode-v0.10.2.patch  /root/source/bitcoind-hubnode-v0.10.2.patch
ADD bitcoind-explorer-v0.10.2.patch /root/source/bitcoind-explorer-v0.10.2.patch
ADD bitcoind-bm-rpc-v0.10.2.patch   /root/source/bitcoind-bm-rpc-v0.10.2.patch

RUN cp -r /root/source/v0.10.2.tar.gz/bitcoin-0.10.2 /root/source/bitcoin-0.10.2
RUN cd /root/source/bitcoin-0.10.2 \
   && patch -p1 < ../bitcoind-hubnode-v0.10.2.patch \
   && patch -p1 < ../bitcoind-explorer-v0.10.2.patch \
   && patch -p1 < ../bitcoind-bm-rpc-v0.10.2.patch \
   && ./autogen.sh \
   && ./configure --without-miniupnpc --disable-wallet \
   && make -j4

RUN cd /root/source/bitcoin-0.10.2 \
   && strip src/bitcoind \
   && cp src/bitcoind /usr/bin/bitcoind-0.10.2 \
   && cp src/bitcoin-cli /usr/bin/bitcoin-cli-0.10.2 \
   && chmod +x /usr/bin/bitcoind-0.10.2 \
   && chmod +x /usr/bin/bitcoin-cli-0.10.2 \
   && ln -s /usr/bin/bitcoind-0.10.2 /usr/bin/bitcoind \
   && ln -s /usr/bin/bitcoin-cli-0.10.2 /usr/bin/bitcoin-cli

# mkdir
RUN mkdir -p /root/supervise_bitcoind
RUN mkdir -p /root/supervise_bitcoind_testnet3
RUN mkdir -p /root/.bitcoin
RUN mkdir -p /root/.bitcoin/testnet3

# scripts
ADD run          /root/supervise_bitcoind/run
ADD run-testnet3 /root/supervise_bitcoind_testnet3/run
RUN chmod +x /root/supervise_bitcoind/run
RUN chmod +x /root/supervise_bitcoind_testnet3/run

# logrotate
ADD logrotate-bitcoind /etc/logrotate.d/bitcoind
ADD logrotate-bitcoind-testnet3 /etc/logrotate.d/bitcoind-testnet3

# startup.sh
RUN mkdir -p /etc/my_init.d
ADD startup-bitcoind.sh /etc/my_init.d/bitcoind.sh
RUN chmod +x /etc/my_init.d/bitcoind.sh

# crontab
ADD bitmain-monitor-bitcoind.sh /root/supervise_bitcoind/bitmain-monitor-bitcoind.sh
ADD bitmain-monitor-bitcoind-testnet3.sh /root/supervise_bitcoind_testnet3/bitmain-monitor-bitcoind-testnet3.sh
ADD crontab.txt /etc/cron.d/bitcoind
