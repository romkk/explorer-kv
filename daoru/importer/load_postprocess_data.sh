#!/bin/bash
set -e

usage() {
	echo "[usage] $0 data_dir" >&2
	exit 0
}

if [[ $# -ne 1 ]]; then
	usage
fi

dir=$1

if [[ ! -d "$dir" ]]; then
	printf "%s not found\n" "$dir" >&2
	exit 1
fi

cd "$dir"

conn="mysql -h127.0.0.1 -uroot -proot -Dbedb0529 -A --local-infile"

for i in `find . -name '*.csv' | sort`; do
	i=${i#./}
	table=`basename "$i" .csv`

	#### create table
	if [[ "$table" = "address_txs_"* ]]; then
		sql="create table if not exists $table like 0_tpl_address_txs"
		$conn <<<"$sql"
	fi

	#### insert table
	sql='load data local infile "%s" into table %s fields terminated by ","'

	case "$table" in
		address_unspent_outputs_*)
			sql="$sql (address_id, tx_id, position, position2, block_height, value, output_script_type, created_at);"
		;;
		block_txs_*)
			sql="$sql (block_id, position, tx_id, created_at);"
		;;
		address_txs_*)
			sql="$sql (address_id, tx_id, tx_height, total_received, balance_diff, balance_final, idx, prev_ymd, prev_tx_id, next_ymd, next_tx_id, created_at);"
		;;
		tx_inputs_*)
			sql="$sql (tx_id, position, input_script_asm, input_script_hex, sequence, prev_tx_id, prev_position, prev_value, prev_address, prev_address_ids, created_at);"
		;;
		tx_outputs_*)
			sql="$sql (tx_id, position, address, address_ids, value, output_script_asm, output_script_hex, output_script_type, spent_tx_id, spent_position, created_at, updated_at);"
		;;
		*)
			sql="$sql;"
		;;
	esac

	sql="
truncate %s;
set foreign_key_checks = 0;
set unique_checks = 0;
set sql_log_bin = 0;
set autocommit = 0;

$sql

set unique_checks = 1;
set foreign_key_checks = 1;
commit;";

	sql=`printf "$sql" "$table" "$table".csv "$table"`

	printf 'insert into %s\n' "$table"
	$conn <<<"$sql"
done