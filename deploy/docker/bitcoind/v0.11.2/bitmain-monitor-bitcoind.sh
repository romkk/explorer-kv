#! /bin/bash
#
# bitcoind monitor
# @copyright tangpool.com
# @author PanZhibiao
# @since 2014-12
#
SROOT=$(cd $(dirname "$0"); pwd)
cd $SROOT

BITCOIND_RPC="bitcoin-cli "
#WANIP=`ip addr | grep 'state UP' -A2 | tail -n1 | awk '{print $2}' | cut -f1  -d'/'`
#WANIP=`curl https://api.ipify.org`
WANIP=`curl http://ipinfo.io/ip`

NOERROR=`$BITCOIND_RPC getinfo |  grep '"errors" : ""' | wc -l`
HEIGHT=`$BITCOIND_RPC getinfo | grep "blocks" | awk '{print $3}' | awk -F"," '{print $1}'`
CONNS=`$BITCOIND_RPC getinfo | grep "connections" | awk '{print $3}' | awk -F"," '{print $1}'`
SCORE=`$BITCOIND_RPC getnetworkinfo | grep "score" | awk '{print $3}' | awk -F"," '{print $1}' | head -1`

SERVICE="explorer.bitcoind.$WANIP"
VALUE="height:$HEIGHT;conn:$CONNS;score:$SCORE"
MURL="http://monitor.bitmain.com/monitor/api/v1/message?service=$SERVICE&value=$VALUE"

if [[ $NOERROR -eq 1 ]] && [[ $CONNS -ne 0 ]]; then
  wget --timeout=20 --tries=3 -O- -q $MURL
  exit 0
fi
