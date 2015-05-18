#!/bin/bash

db=`grep -Po '(?<=DATABASE_NAME\=).+$' .env`
user=`grep -Po '(?<=DATABASE_USER\=).+$' .env`
pass=`grep -Po '(?<=DATABASE_PASS\=).+$' .env`
port=`grep -Po '(?<=DATABASE_PORT\=).+$' .env`
host=`grep -Po '(?<=DATABASE_HOST\=).+$' .env`

cd "$(dirname "$0")"

if [[ $# != 1 ]]; then
	printf '%s dir' $0
	exit 0
fi

if [[ ! -d "$1" ]]; then
	printf '%s not found' `pwd`/$1
	exit 1
fi

cd "$1"

conn="mysql -h"$host" -P"$port" -u"$user" -p"$pass" -D"$db" --local-infile"

##### raw_blocks
raw_blocks="load data local infile '0_raw_blocks'
into table 0_raw_blocks
fields terminated by ','
(block_hash, block_height, chain_id, hex, created_at)
"

echo "$raw_blocks" | $conn


##### txlogs
tx_logs="load data local infile '%s'
into table %s
fields terminated by ','
(handle_status, handle_type, block_height, block_timestamp, tx_hash, created_at, updated_at)
"

for f in `find . -name 'txlogs_*'`; do
	f=${f#./}
	printf "create table if not exists %s like 0_tpl_txlogs" "$f" | $conn
	printf "$tx_logs" "$f" "$f" | $conn
done

##### raw_txs
raw_txs="load data local infile '%s'
into table %s
fields terminated by ','
(id, tx_hash, hex, created_at)
"

for f in `find . -name 'raw_txs_*'`; do
	f=${f#./}
	printf "$raw_txs" "$f" "$f" | $conn
done
