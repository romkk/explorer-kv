var mysql = require('./mysql');
var helper = require('./helper');
var sprintf = require('sprintf').sprintf;
var log = require('debug')('api:lib:block');
var moment = require('moment');
var Tx = require('./tx');
var sb = require('./ssdb')();
var _ = require('lodash');

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

    load() {
        var table = Block.getBlockTxTableByBlockId(this.attrs.block_id);
        var sql = `select tx_id
                   from ${table}
                   where block_id = ?
                   order by position asc`;

        return mysql.list(sql, 'tx_id', [ this.attrs.block_id ])
            .then(txIndexes => {
                this.txs = txIndexes;

                log(`set cache blk_id = ${this.attrs.block_id}`);

                // set cache，对于可能存在的边界情况全部忽略
                sb.multi_set(
                    `blkid_${this.attrs.block_id}`, this.attrs.hash,    //blkid_{id} => hash
                    `blk_${this.attrs.hash}`, JSON.stringify(this)
                );

                return this;
            });
    }

    static make(id) {
        var idType = helper.paramType(id);
        var sql = `select *
                   from 0_blocks
                   where ${idType == helper.constant.HASH_IDENTIFIER ? 'hash' : 'block_id'} = ?
                   limit 1`;
        return mysql.selectOne(sql, [id])
            .then(blk => {
                return blk == null ? null : new Block(blk);
            });
    }

    static grab(id, offset = 0, limit = 50, fulltx = false, useCache = true) {
        var idType = helper.paramType(id);
        var p = Promise.resolve(id);

        if (useCache) {
            if (idType === helper.constant.ID_IDENTIFIER) {
                p = sb.get(`blkid_${id}`)
                    .then(v => {
                        if (v == null) {
                            log(`[cache miss] blk_query = ${id}`);
                            return Promise.reject();
                        }
                        return v;
                    });
            }

            p = p.then(hash => sb.get(`blk_${hash}`))
                .then(v => {
                    if (v == null) {
                        log(`[cache miss] blk_query = ${id}`);
                        return Promise.reject();
                    }

                    log(`[cache hit] blk_query = ${id}`);

                    return JSON.parse(v);
                });
        } else {
            p = Promise.reject();
        }

        return p.catch(() => {
            return Block.make(id)
                .then(blk => {
                    if (blk == null) {
                        return Promise.reject();
                    }
                    return blk.load();
                })
                .then(blk => blk.toJSON());
        }).then(blk => {
            var txs;

            if (offset == 0 && limit == 0) {
                txs = blk.tx;
            } else {
                txs = blk.tx.splice(offset, limit);
            }

            if (!fulltx) {
                blk.tx = txs;
                return blk;
            }

            return Tx.multiGrab(txs, useCache)
                .then(items => {
                    blk.tx = items;
                    return blk;
                });
        });
    }

    static getBlockTxTableByBlockId(blockId) {
        return sprintf('block_txs_%04d', parseInt(blockId, 10) % 100);
    }

    static getLatestHeight() {
        var sql = `select height from 0_blocks
                   where chain_id = 0 order by height desc limit 1`;
        return mysql.pluck(sql, 'height');
    }
}

Block.grabByHeight = async (h, useCache = true) => {
    var getHashListFromDb = async () => {
        let sql = `select hash from 0_blocks
                   where height = ? order by chain_id asc`;
        blockHashList = await mysql.list(sql, 'hash', [h]);

        if (blockHashList.length) {
            sb.set(`blkh_${h}`, JSON.stringify(blockHashList));
        }

        return blockHashList;
    };

    if (useCache) {
        var blockHashList = await sb.get(`blkh_${h}`);
        if (blockHashList == null) {
            blockHashList = getHashListFromDb();
        } else {
            blockHashList = JSON.parse(blockHashList);
        }
    } else {
        blockHashList = getHashListFromDb();
    }

    return await* blockHashList.map(hash => Block.grab(hash, 0, 0, false, useCache));
};

module.exports = Block;