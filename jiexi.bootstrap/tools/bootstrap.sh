#!/bin/bash
SROOT=$(cd $(dirname "$0"); pwd)
cd $SROOT
export LC_ALL=C

echo > pre_tx.log
echo > pre_addr.log
echo > pre_parser.log
echo > compactdb.log
./pre_tx -c "tparser.bootstrap.conf" -l "pre_tx.log" &
./pre_addr -c "tparser.bootstrap.conf" -l "pre_addr.log" &

wait
./pre_parser -c "tparser.bootstrap.conf" -l "pre_parser.log"
./compactdb -c "tparser.bootstrap.conf" -l "compactdb.log"
