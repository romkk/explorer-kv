var mysql = require('./mysql');
var helper = require('./helper');
var sprintf = require('sprintf').sprintf;
var log = require('debug')('api:lib:block');
var moment = require('moment');
var Tx = require('./tx');

class Block {
    constructor(row) {
        this.attrs = row;
    }

    get txs() {
        if (this._txs == null) {
            throw new Error('Block instance has not been loaded');
        }
        return this._txs;
    }

    set txs(v) {
        this._txs = v;
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

    load(fulltx) {
        var table = Block.getBlockTxTableByBlockId(this.attrs.block_id);
        var sql = `select tx_id
                   from ${table}
                   where block_id = ?
                   order by position asc`;
        return mysql.list(sql, 'tx_id', [ this.attrs.block_id ])
            .then(txIndexes => {
                if (fulltx) {
                    var promises = txIndexes.map(id => {
                        return Tx.make(id)
                            .then((tx) => {
                                return tx.load();
                            });
                    });
                    return Promise.all(promises);
                } else {
                    return txIndexes;
                }
            })
            .then(txs => {
                this.txs = txs;
                return this;
            });
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
        return sprintf('block_txs_%04d', parseInt(blockId, 10) % 100);
    }
}

module.exports = Block;