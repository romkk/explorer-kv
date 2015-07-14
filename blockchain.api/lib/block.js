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
            chain_id: this.attrs.chain_id,
            height: this.attrs.height,
            tx: this.txs,
            relayed_by: this.attrs.relayed_by || 'Unknown',
            difficulty: this.attrs.difficulty
        };
    }

    async load() {
        var table = Block.getBlockTxTableByBlockId(this.attrs.block_id);
        var sql = `select tx_id from ${table}
                   where block_id = ? order by position asc`;

        this.txs = await mysql.list(sql, 'tx_id', [ this.attrs.block_id ]);

        log(`set cache blk_id = ${this.attrs.block_id}`);
        // set cache，对于可能存在的边界情况全部忽略
        sb.multi_set(
            `blkid_${this.attrs.block_id}`, this.attrs.hash,    //blkid_{id} => hash
            `blk_${this.attrs.hash}`, JSON.stringify(this)
        );

        return this;
    }

    static async getNextBlock(currentHeight, chainId, useCache) {
        let h = currentHeight + 1;
        let blks = await Block.grabByHeight(h, useCache);
        if (!blks.length) return null;
        let next = blks.find(b => b.chain_id == chainId);
        return next || null;
    }

    static make(id) {
        var idType = helper.paramType(id);
        var sql = `select v1.*, v2.name as relayed_by
                   from 0_blocks v1
                        left join 0_pool v2
                            on v1.relayed_by = v2.pool_id
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

    static async multiGrab(idList, useCache) {
        if (!useCache) {
            return (await* idList.map(async (id) => {
                try {
                    return (await Block.grab(id, 0, 0, false, false));
                } catch (err) {
                    return null;
                }
            }));
        }

        let idType = helper.paramType(idList[0]);
        let bag = _.zipObject(idList);
        if (idType === helper.constant.ID_IDENTIFIER) {
            let hashList = sb.multi_get.apply(sb, idList.map(id => `blkid_${id}`));
            let cachedHash = _.chain(hashList).chunk(2).zipObject().mapKeys((v, k) => k.slice(6)).value();
            let missedIds = idList.filter(id => !cachedHashList[id]);
            let p1 = missedIds.map(async (id) => {
                try {
                    return (await Block.grab(id, 0, 0, false, true));
                } catch (err) {
                    return null;
                }
            });
            let p2 = Block.multiGrab(Object.values(cachedHash), true);
            _.flatten(await* p1.concat(p2)).forEach(blk => bag[blk.id] = blk);
            return idList.map(id => bag[id]);
        } else {    // hash list
            let cached = await sb.multi_get.apply(sb, idList.map(id => `blk_${id}`));
            cached = _.chain(cached).chunk(2).zipObject().mapKeys((v, k) => k.slice(4)).mapValues(JSON.parse).value();
            _.extend(bag, cached);
            let missed = await* idList.filter(id => !cached[id]).map(async (hash) => {
                    try {
                        return (await Block.grab(hash, 0, 0, false, true));
                    } catch (err) {
                        return null;
                    }
                });
            missed.forEach(blk => bag[blk.hash] = blk);
            return idList.map(id => bag[id]);
        }
    }

    static getBlockTxTableByBlockId(blockId) {
        return sprintf('block_txs_%04d', parseInt(blockId, 10) % 100);
    }

    static getLatestHeight() {
        var sql = `select height from 0_blocks
                   where chain_id = 0 order by height desc limit 1`;
        return mysql.pluck(sql, 'height');
    }

    static async getBlockHeightByTimestamp(timestamp, order) {
        let sql = `select height from 0_blocks where
                    \`timestamp\` ${order === 'desc' ? '<=' : '>='} ?
                    and chain_id = 0
                   order by height ${order} limit 1`;
        return (await mysql.pluck(sql, 'height', [timestamp]));
    }

    static async getBlockList(timestamp, offset, limit, order, useCache) {
        let sql = `select hash from 0_blocks
                   where chain_id = 0 and timestamp ${order == 'desc' ? '<=' : '>='} ?
                   order by height ${order}
                   limit ${offset}, ${limit}`;
        let hashList = await mysql.list(sql, 'hash', [timestamp]);
        return (await Block.multiGrab(hashList, useCache));
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