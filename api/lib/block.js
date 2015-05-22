var mysql = require('./mysql');
var helper = require('./helper');
var sprintf = require('sprintf').sprintf;
var log = require('debug')('api:lib:tx');
var moment = require('moment');

class Block {
    constructor(row) {
        this.attrs = row;
    }

    toJSON() {
        return {
            hash: this.attrs.hash,
            ver: this.attrs.version,
            prev_block: this.attrs.prev_block_hash,
            mrkl_root: this.attrs.mrkl_root,
            time: this.attrs.timestamp,
            bits: this.attrs.bits,
            fee: this.attrs.reward_fees,
            nonce: this.attrs.nonce,
            n_tx: this.attrs.tx_count,
            size: this.attrs.size,
            block_index: this.attrs.block_id,
            main_chain: this.attrs.chain_id === 0,
            height: this.attrs.height,
            tx: this.txs
        };
    }

    static make(id) {
        var idType = helper.paramType(id);
        var sql = `select *
                   from 0_blocks
                   where ${idType == helper.constant.HASH_IDENTIFIER ? 'hash' : 'height'} = ? and chain_id = 0`;
        return mysql.selectOne(sql, [id])
            .then(blk => {
                return blk == null ? null : new Block(blk);
            });
    }

    static getBlockTxTableByBlockId(blockId) {
        return sprintf('block_txs_%04d', parseInt(blockId, 10) % 64);
    }
}

module.exports = Block;