#! /bin/bash
PATH=/usr/local/sbin:/usr/local/bin:/sbin:/bin:/usr/sbin:/usr/bin

nohup supervise /root/supervise_bitcoind > /dev/null &
