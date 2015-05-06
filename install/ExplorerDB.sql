-- Adminer 4.2.1 MySQL dump

SET NAMES utf8;
SET time_zone = '+00:00';
SET foreign_key_checks = 0;
SET sql_mode = 'NO_AUTO_VALUE_ON_ZERO';

DROP TABLE IF EXISTS `addresses`;
CREATE TABLE `addresses` (
  `id` bigint(20) NOT NULL AUTO_INCREMENT,
  `address` varchar(35) NOT NULL,
  `tx_count` int(11) NOT NULL DEFAULT '0',
  `total_received` bigint(20) NOT NULL DEFAULT '0',
  `total_sent` bigint(20) NOT NULL DEFAULT '0',
  `created_at` datetime NOT NULL,
  `updated_at` datetime NOT NULL,
  PRIMARY KEY (`id`),
  UNIQUE KEY `address` (`address`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;


DROP TABLE IF EXISTS `address_txs`;
CREATE TABLE `address_txs` (
  `id` bigint(20) NOT NULL AUTO_INCREMENT,
  `address_id` bigint(20) NOT NULL,
  `tx_id` bigint(20) NOT NULL,
  `tx_height` bigint(20) NOT NULL,
  `total_received` bigint(20) NOT NULL,
  `balance_diff` bigint(20) NOT NULL,
  `balance_final` bigint(20) NOT NULL,
  `prev_ymd` int(11) NOT NULL,
  `next_ymd` int(11) NOT NULL,
  `created_at` datetime NOT NULL,
  PRIMARY KEY (`id`),
  UNIQUE KEY `address_id_counter_chain_id` (`address_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;


DROP TABLE IF EXISTS `blocks`;
CREATE TABLE `blocks` (
  `block_id` bigint(20) NOT NULL,
  `height` bigint(20) NOT NULL,
  `hash` char(64) NOT NULL,
  `version` int(11) NOT NULL,
  `mrkl_root` char(64) NOT NULL,
  `timestamp` bigint(20) NOT NULL,
  `bits` bigint(20) NOT NULL,
  `nonce` bigint(20) NOT NULL,
  `prev_block_id` bigint(20) NOT NULL,
  `next_block_id` bigint(20) NOT NULL,
  `chain_id` int(11) NOT NULL,
  `size` int(11) NOT NULL,
  `difficulty` bigint(20) NOT NULL,
  `tx_count` int(11) NOT NULL,
  `reward_block` bigint(20) NOT NULL,
  `reward_fees` bigint(20) NOT NULL,
  `created_at` datetime NOT NULL,
  UNIQUE KEY `block_hash` (`hash`),
  UNIQUE KEY `height_chain_id` (`height`,`chain_id`),
  UNIQUE KEY `block_id` (`block_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;


DROP TABLE IF EXISTS `block_txs`;
CREATE TABLE `block_txs` (
  `id` int(11) NOT NULL AUTO_INCREMENT,
  `block_id` bigint(20) NOT NULL,
  `position` int(11) NOT NULL,
  `tx_id` bigint(20) NOT NULL,
  `created_at` datetime NOT NULL,
  PRIMARY KEY (`id`),
  UNIQUE KEY `block_id_position` (`block_id`,`position`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;


DROP TABLE IF EXISTS `explorer_meta`;
CREATE TABLE `explorer_meta` (
  `id` int(11) NOT NULL AUTO_INCREMENT,
  `key` varchar(64) NOT NULL,
  `value` varchar(1024) NOT NULL,
  `created_at` datetime NOT NULL,
  `updated_at` datetime NOT NULL,
  PRIMARY KEY (`id`),
  UNIQUE KEY `key` (`key`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;


DROP TABLE IF EXISTS `raw_blocks`;
CREATE TABLE `raw_blocks` (
  `id` bigint(20) NOT NULL AUTO_INCREMENT,
  `block_hash` char(64) NOT NULL,
  `hex` longtext NOT NULL,
  `created_at` datetime NOT NULL,
  PRIMARY KEY (`id`),
  UNIQUE KEY `block_id` (`block_hash`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;


DROP TABLE IF EXISTS `raw_txs`;
CREATE TABLE `raw_txs` (
  `id` bigint(20) NOT NULL AUTO_INCREMENT,
  `tx_hash` char(64) NOT NULL,
  `hex` longtext NOT NULL,
  `created_at` datetime NOT NULL,
  PRIMARY KEY (`id`),
  UNIQUE KEY `tx_id` (`tx_hash`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;


DROP TABLE IF EXISTS `txlogs`;
CREATE TABLE `txlogs` (
  `id` bigint(20) NOT NULL AUTO_INCREMENT,
  `handle_status` int(11) NOT NULL,
  `handle_type` int(11) NOT NULL,
  `block_height` bigint(20) NOT NULL,
  `tx_hash` char(64) NOT NULL,
  `created_at` datetime NOT NULL,
  `updated_at` datetime NOT NULL,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;


DROP TABLE IF EXISTS `txs`;
CREATE TABLE `txs` (
  `tx_id` bigint(20) NOT NULL,
  `hash` char(64) NOT NULL,
  `height` bigint(20) NOT NULL,
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
  UNIQUE KEY `hash` (`hash`),
  UNIQUE KEY `tx_id` (`tx_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;


DROP TABLE IF EXISTS `tx_inputs`;
CREATE TABLE `tx_inputs` (
  `id` int(11) NOT NULL AUTO_INCREMENT,
  `tx_id` bigint(20) NOT NULL,
  `position` int(11) NOT NULL,
  `script` text NOT NULL,
  `sequence` bigint(20) NOT NULL,
  `prev_tx_id` bigint(20) NOT NULL,
  `prev_position` int(11) NOT NULL,
  `prev_address_id` bigint(20) NOT NULL,
  `prev_value` bigint(20) NOT NULL,
  `created_at` datetime NOT NULL,
  PRIMARY KEY (`id`),
  UNIQUE KEY `tx_id_position` (`tx_id`,`position`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;


DROP TABLE IF EXISTS `tx_outputs`;
CREATE TABLE `tx_outputs` (
  `id` int(11) NOT NULL AUTO_INCREMENT,
  `tx_id` bigint(20) NOT NULL,
  `position` int(11) NOT NULL,
  `address_id` bigint(20) NOT NULL,
  `value` bigint(20) NOT NULL,
  `script` text NOT NULL,
  `spent_tx_id` bigint(20) NOT NULL,
  `spent_position` int(11) NOT NULL,
  `created_at` datetime NOT NULL,
  PRIMARY KEY (`id`),
  UNIQUE KEY `tx_id_position` (`tx_id`,`position`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;


DROP TABLE IF EXISTS `unconfirmed_txs_list`;
CREATE TABLE `unconfirmed_txs_list` (
  `id` int(11) NOT NULL AUTO_INCREMENT,
  `tx_id` bigint(20) NOT NULL,
  `unconfirmed_height` bigint(20) NOT NULL,
  `created_at` datetime NOT NULL,
  PRIMARY KEY (`id`),
  UNIQUE KEY `tx_id` (`tx_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;


DROP TABLE IF EXISTS `unspent_outputs`;
CREATE TABLE `unspent_outputs` (
  `id` int(11) NOT NULL AUTO_INCREMENT,
  `tx_id` bigint(20) NOT NULL,
  `position` int(11) NOT NULL,
  `block_height` bigint(20) NOT NULL,
  `value` bigint(20) NOT NULL,
  `created_at` int(11) NOT NULL,
  PRIMARY KEY (`id`),
  UNIQUE KEY `tx_id_position` (`tx_id`,`position`),
  KEY `tx_id_block_height` (`tx_id`,`block_height`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;


-- 2015-05-06 02:02:49
