#!/bin/bash
set -e

# twiki: http://twiki.bitmain.com/bin/view/Main/ExplorerDB

loop() {
	start=$1
	end=$2
	tpl=$3

	for i in `seq "$start" "$end"`; do
		printf "$tpl"'\n' $i $i
	done
}

echo 'SET NAMES utf8;'

# preset tables

## raw_txs: 0000-0063
tpl_raw_txs='
DROP TABLE IF EXISTS `raw_txs_%04d`;
CREATE TABLE `raw_txs_%04d` (
  `id` bigint(20) NOT NULL,
  `tx_hash` char(64) NOT NULL,
  `hex` longtext NOT NULL,
  `created_at` datetime NOT NULL,
  PRIMARY KEY (`id`),
  UNIQUE KEY `tx_hash` (`tx_hash`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;'

loop 0 63 "$tpl_raw_txs"

## address: 0000-0063
tpl_address='
DROP TABLE IF EXISTS `addresses_%04d`;
CREATE TABLE `addresses_%04d` (
  `id` bigint(20) NOT NULL,
  `address` varchar(35) NOT NULL,
  `tx_count` int(11) NOT NULL DEFAULT '0',
  `total_received` bigint(20) NOT NULL DEFAULT '0',
  `total_sent` bigint(20) NOT NULL DEFAULT '0',
  `begin_tx_id` bigint(20) NOT NULL DEFAULT '0',
  `begin_tx_ymd` int(11) NOT NULL DEFAULT '0',
  `end_tx_id` bigint(20) NOT NULL DEFAULT '0',
  `end_tx_ymd` int(11) NOT NULL DEFAULT '0',
  `created_at` datetime NOT NULL,
  `updated_at` datetime NOT NULL,
  PRIMARY KEY (`id`),
  UNIQUE KEY `address` (`address`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;'

loop 0 63 "$tpl_address"

## block_txs: 0000-0099
tpl_block_txs='
DROP TABLE IF EXISTS `block_txs_%04d`;
CREATE TABLE `block_txs_%04d` (
  `id` int(11) NOT NULL AUTO_INCREMENT,
  `block_id` bigint(20) NOT NULL,
  `position` int(11) NOT NULL,
  `tx_id` bigint(20) NOT NULL,
  `created_at` datetime NOT NULL,
  PRIMARY KEY (`id`),
  UNIQUE KEY `block_id_position` (`block_id`,`position`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;'

loop 0 99 "$tpl_block_txs"

## txs: 0000-0063
tpl_txs='
DROP TABLE IF EXISTS `txs_%04d`;
CREATE TABLE `txs_%04d` (
  `tx_id` bigint(20) NOT NULL,
  `hash` char(64) NOT NULL,
  `height` bigint(20) NOT NULL,
  `block_timestamp` bigint(20) NOT NULL,
  `is_coinbase` tinyint(1) NOT NULL,
  `version` int(11) NOT NULL,
  `lock_time` bigint(20) NOT NULL,
  `size` int(11) NOT NULL,
  `fee` bigint(20) NOT NULL,
  `total_in_value` bigint(20) NOT NULL,
  `total_out_value` bigint(20) NOT NULL,
  `inputs_count` int(11) NOT NULL,
  `outputs_count` int(11) NOT NULL,
  `created_at` datetime NOT NULL,
  PRIMARY KEY (`tx_id`),
  UNIQUE KEY `hash` (`hash`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;'

loop 0 63 "$tpl_txs"

## tx_inputs: 0000-0099
tpl_tx_inputs='
DROP TABLE IF EXISTS `tx_inputs_%04d`;
CREATE TABLE `tx_inputs_%04d` (
  `id` bigint(20) NOT NULL AUTO_INCREMENT,
  `tx_id` bigint(20) NOT NULL,
  `position` int(11) NOT NULL,
  `input_script_asm` text NOT NULL,
  `input_script_hex` text NOT NULL,
  `sequence` bigint(20) NOT NULL,
  `prev_tx_id` bigint(20) NOT NULL,
  `prev_position` int(11) NOT NULL,
  `prev_address_id` bigint(20) NOT NULL,
  `prev_value` bigint(20) NOT NULL,
  `prev_address` varchar(1024) NOT NULL,
  `prev_address_ids` varchar(512) NOT NULL,
  `created_at` datetime NOT NULL,
  PRIMARY KEY (`id`),
  UNIQUE KEY `tx_id_position` (`tx_id`,`position`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;'

loop 0 99 "$tpl_tx_inputs"

## tx_outputs: 0000-0099
tpl_tx_outputs='
DROP TABLE IF EXISTS `tx_outputs_%04d`;
CREATE TABLE `tx_outputs_%04d` (
  `id` bigint(20) NOT NULL AUTO_INCREMENT,
  `tx_id` bigint(20) NOT NULL,
  `position` int(11) NOT NULL,
  `address` varchar(1024) NOT NULL,               
  `address_ids` varchar(512) NOT NULL,
  `value` bigint(20) NOT NULL,
  `output_script_asm` longtext NOT NULL,
  `output_script_hex` longtext NOT NULL,
  `output_script_type` enum("NonStandard","PubKey","PubKeyHash","ScriptHash","MultiSig","NullData") NOT NULL,
  `is_spendable` tinyint(1) NOT NULL,
  `spent_tx_id` bigint(20) NOT NULL,
  `spent_position` int(11) NOT NULL,
  `created_at` datetime NOT NULL,
  `updated_at` datetime NOT NULL,
  PRIMARY KEY (`id`),
  UNIQUE KEY `tx_id_position` (`tx_id`,`position`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;'

loop 0 99 "$tpl_tx_outputs"

## unspent_outputs: 0000-0009
tpl_unspent_outputs='
DROP TABLE IF EXISTS `address_unspent_outputs_%04d`;
CREATE TABLE `address_unspent_outputs_%04d` (
  `id` int(11) NOT NULL AUTO_INCREMENT,
  `address_id` bigint(20) NOT NULL,
  `tx_id` bigint(20) NOT NULL,
  `position` smallint(6) NOT NULL,
  `position2` smallint(6) NOT NULL DEFAULT '0',
  `block_height` bigint(20) NOT NULL,
  `value` bigint(20) NOT NULL,
  `created_at` int(11) NOT NULL,
  PRIMARY KEY (`id`),
  UNIQUE KEY `address_id_tx_id_position_position2` (`address_id`,`tx_id`,`position`,`position2`),
  KEY `address_id_block_height` (`address_id`,`block_height`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;'

loop 0 9 "$tpl_unspent_outputs"

# table template
tpl_txlogs='
DROP TABLE IF EXISTS `0_tpl_txlogs`;
CREATE TABLE `0_tpl_txlogs` (
  `id` bigint(20) NOT NULL AUTO_INCREMENT,
  `handle_status` int(11) NOT NULL,
  `handle_type` int(11) NOT NULL,
  `block_height` bigint(20) NOT NULL,
  `block_timestamp` bigint(20) NOT NULL,
  `tx_hash` char(64) NOT NULL,
  `created_at` datetime NOT NULL,
  `updated_at` datetime NOT NULL,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;'

echo "$tpl_txlogs"

tpl_address_txs='
DROP TABLE IF EXISTS `0_tpl_address_txs`;
CREATE TABLE `0_tpl_address_txs` (
  `id` bigint(20) NOT NULL AUTO_INCREMENT,
  `address_id` bigint(20) NOT NULL,
  `tx_id` bigint(20) NOT NULL,
  `tx_height` bigint(20) NOT NULL,
  `total_received` bigint(20) NOT NULL,
  `balance_diff` bigint(20) NOT NULL,
  `balance_final` bigint(20) NOT NULL,
  `prev_ymd` int(11) NOT NULL,
  `prev_tx_id` bigint(20) NOT NULL,
  `next_ymd` int(11) NOT NULL,
  `next_tx_id` bigint(20) NOT NULL,
  `created_at` datetime NOT NULL,
  PRIMARY KEY (`id`),
  UNIQUE KEY `address_id_tx_id` (`address_id`,`tx_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;'

echo "$tpl_address_txs"

# normal tables

raw_blocks='
DROP TABLE IF EXISTS `0_raw_blocks`;
CREATE TABLE `0_raw_blocks` (
  `id` bigint(20) NOT NULL AUTO_INCREMENT,
  `block_hash` char(64) NOT NULL,
  `block_height` int(11) NOT NULL,
  `chain_id` int(11) NOT NULL DEFAULT '0',
  `hex` longtext NOT NULL,
  `created_at` datetime NOT NULL,
  PRIMARY KEY (`id`),
  UNIQUE KEY `block_id` (`block_hash`),
  UNIQUE KEY `block_height_chain_id` (`block_height`,`chain_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;'

echo "$raw_blocks"

explorer_meta='
DROP TABLE IF EXISTS `0_explorer_meta`;
CREATE TABLE `0_explorer_meta` (
  `id` int(11) NOT NULL AUTO_INCREMENT,
  `key` varchar(64) NOT NULL,
  `value` varchar(1024) NOT NULL,
  `created_at` datetime NOT NULL,
  `updated_at` datetime NOT NULL,
  PRIMARY KEY (`id`),
  UNIQUE KEY `key` (`key`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;'

echo "$explorer_meta"

blocks='
DROP TABLE IF EXISTS `0_blocks`;
CREATE TABLE `0_blocks` (
  `block_id` bigint(20) NOT NULL,
  `height` bigint(20) NOT NULL,
  `hash` char(64) NOT NULL,
  `version` int(11) NOT NULL,
  `mrkl_root` char(64) NOT NULL,
  `timestamp` bigint(20) NOT NULL,
  `bits` bigint(20) NOT NULL,
  `nonce` bigint(20) NOT NULL,
  `prev_block_id` bigint(20) NOT NULL,
  `prev_block_hash` char(64) NOT NULL,
  `next_block_id` bigint(20) NOT NULL,
  `next_block_hash` char(64) NOT NULL,
  `chain_id` int(11) NOT NULL,
  `size` int(11) NOT NULL,
  `difficulty` bigint(20) NOT NULL,
  `tx_count` int(11) NOT NULL,
  `reward_block` bigint(20) NOT NULL,
  `reward_fees` bigint(20) NOT NULL,
  `created_at` datetime NOT NULL,
  PRIMARY KEY (`block_id`),
  UNIQUE KEY `block_hash` (`hash`),
  UNIQUE KEY `height_chain_id` (`height`,`chain_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;'

echo "$blocks"

## TODO: unconfirmed_txs_list
