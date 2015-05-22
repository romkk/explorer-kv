var sprintf = require('sprintf').sprintf;
var mysql = require('./mysql');
var log = require('debug')('api:helper');

module.exports = {
    getBlockTxTableByBlockId(blockId) {
        return sprintf('block_txs_%04d', parseInt(blockId, 10) % 64);
    }
};