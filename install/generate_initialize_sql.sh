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


# 0_txlogs2
txlogs2='
DROP TABLE IF EXISTS `0_txlogs2`;
CREATE TABLE `0_txlogs2` (
  `id` bigint(20) NOT NULL AUTO_INCREMENT,
  `batch_id` bigint(20) NOT NULL,
  `type` int(11) NOT NULL,
  `block_height` int(11) NOT NULL,
  `block_id` bigint(20) NOT NULL,
  `max_block_timestamp` bigint(20) NOT NULL,
  `tx_hash` char(64) NOT NULL,
  `created_at` datetime NOT NULL,
  `updated_at` datetime NOT NULL,
  PRIMARY KEY (`id`),
  KEY `batch_id` (`batch_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;'

echo "$txlogs2"

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

memrepo_txs='
DROP TABLE IF EXISTS `0_memrepo_txs`;
CREATE TABLE `0_memrepo_txs` (
  `position` bigint(20) NOT NULL AUTO_INCREMENT,
  `tx_hash` char(64) NOT NULL,
  `created_at` datetime NOT NULL,
  PRIMARY KEY (`position`),
  UNIQUE KEY `tx_hash` (`tx_hash`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;'

echo "$memrepo_txs"
