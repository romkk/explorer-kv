var mysql = require('./mysql');
var helper = require('./helper');
var sprintf = require('sprintf').sprintf;
var log = require('debug')('api:lib:tx');
var moment = require('moment');
var sb = require('./ssdb')();
var _ = require('lodash');

/**
 *
 * Coinbase Tx
 * https://blockchain.info/rawtx/9afb8dd3b2b4f9d5ffe21ce69e44287c4216acf53469c7eb7457e6cead9b6283
 *
 * Normal TX
 * https://blockchain.info/rawtx/84ae6680e19da59a948634247648054c45d3bce36bad2f2c8b5d67daa6c03e2e
 *
 */

class Tx {
    constructor(txRow) {
        this.attrs = txRow;
    }

    get inputs() {
        if (this._inputs == null) {
            throw new Error('Tx instance has not been loaded');
        }
        return this._inputs;
    }

    set inputs(v) {
        this._inputs = v;
    }

    get outputs() {
        if (this._outputs == null) {
            throw new Error('Tx instance has not been loaded');
        }
        return this._outputs;
    }

    set outputs(v) {
        this._outputs = v;
    }

    toJSON() {
        var ret = {
            ver: this.attrs.version,
            inputs: this.inputs,
            block_height: this.attrs.height,
            out: this.outputs,
            lock_time: this.attrs.lock_time,
            size: this.attrs.size,
            time: this.attrs.block_timestamp,
            tx_index: this.attrs.tx_id,
            hash: this.attrs.hash,
            vin_sz: this.attrs.inputs_count,
            vout_sz: this.attrs.outputs_count,
            is_coinbase: !!this.attrs.is_coinbase,
            fee: this.attrs.fee,
            total_in_value: this.attrs.total_in_value,
            total_out_value: this.attrs.total_out_value
        };

        /*
         if (!this.attrs.is_coinbase) {
         ret.double_spend = null;
         }
         */

        return ret;
    }

    load() {    //加载全部数据
        var sql;

        // 获取 inputs
        sql = `SELECT id, tx_id, position, input_script_hex, input_script_asm,
                        sequence, prev_tx_id, prev_position, prev_value, prev_address,
                        prev_address_ids, created_at
                FROM \`${this.getInputTable()}\`
                WHERE \`tx_id\` = ?
                ORDER BY position asc`;
        var inputPromise = mysql.query(sql, [this.attrs.tx_id])
            .then((rows) => {
                this.inputs = rows.map(r => {
                    let ret = {};
                    ret.sequence = r.sequence;
                    ret.script = r.input_script_hex;
                    ret.script_asm = r.input_script_asm;
                    if (!this.attrs.is_coinbase) {
                        ret.prev_out = {        //omit `spent` and `script`
                            tx_index: r.prev_tx_id,
                            //type: null,     //tbd
                            addr: r.prev_address.split(/[,|]/),     //地址可能以逗号或者竖线分割
                            value: r.prev_value,
                            n: r.prev_position
                        };
                    }
                    return ret;
                });
            });


        // 获取 outputs
        sql = `SELECT tx_id, position, address, address_ids, value, output_script_asm, output_script_hex, spent_tx_id
                FROM \`${this.getOutputTable()}\`
                WHERE \`tx_id\` = ?
                ORDER BY position asc`;
        var outputPromise = mysql.query(sql, [this.attrs.tx_id])
            .then((rows) => {
                this.outputs = rows.map(r => {
                    let ret = {};
                    ret.spent = r.spent_tx_id != 0;
                    ret.tx_index = r.spent_tx_id || null;
                    //ret.type = null;
                    ret.addr = r.prev_address.split(/[,|]/);     //地址可能以逗号或者竖线分割
                    ret.value = r.value;
                    ret.n = r.position;
                    ret.script = r.output_script_hex;
                    ret.script_asm = r.output_script_asm;

                    if (this.attrs.is_coinbase) {
                        ret.addr_tag_link = null;
                        ret.addr_tag = null;
                    }
                    return ret;
                });
            });

        return Promise.all([inputPromise, outputPromise])
            .then(() => {
                log(`set cache tx_id = ${this.attrs.tx_id}, tx_hash = ${this.attrs.hash}`);
                sb.multi_set(
                    `txid_${this.attrs.tx_id}`, this.attrs.hash,
                    `tx_${this.attrs.hash}`, JSON.stringify(this)
                );

                return this;
            });
    }

    static make(id) {
        var idType = helper.paramType(id);
        var table = idType == helper.constant.HASH_IDENTIFIER ? Tx.getTableByHash(id) : Tx.getTableById(id);
        var sql = `select v1.tx_id, v1.hash, v1.height, v1.is_coinbase,
                   v1.version, v1.lock_time, v1.size, v1.fee, v1.total_in_value,
                   v1.total_out_value, v1.inputs_count, v1.outputs_count, v1.created_at,
                   ifnull(v2.timestamp, -1) as block_timestamp
                   from ${table} v1
                     left join 0_blocks v2      -- 可能为临时块交易
                       on v1.height = v2.height and v2.chain_id = 0
                   where ${idType == helper.constant.HASH_IDENTIFIER ? `v1.hash = ?` : `v1.tx_id = ?`}`;
        return mysql.selectOne(sql, [id])
            .then((txRow) => {
                if (txRow == null) {
                    return null;
                }
                return new Tx(txRow);
            });
    }

    static grab(id, useCache = true) {
        var idType = helper.paramType(id);
        var p = Promise.resolve(id);

        if (useCache) {
            if (idType == helper.constant.ID_IDENTIFIER) {
                p = p.then(() => sb.get(`txid_${id}`))
                    .then(v => {
                        if (v == null) {
                            log(`[cache miss] tx_query = ${id}`);
                            return Promise.reject();    //查无此 hash，即缓存内不存在
                        }
                        return v;
                    });
            }

            p = p.then(hash => sb.get(`tx_${hash}`))
                .then(v => {
                    if (v == null) {
                        log(`[cache miss] tx_query = ${id}`);
                        return Promise.reject();
                    }
                    log(`[cache hit] tx_query = ${id}`);
                    return JSON.parse(v);
                });
        } else {
            p = Promise.reject();
        }

        return p.catch(() => {
            return Tx.make(id)
                .then(tx => {
                    if (tx == null) {
                        return Promise.reject();
                    }
                    return tx.load();
                })
                .then(tx => {
                    return tx.toJSON();
                });
        });
    }

    static multiGrab(ids, useCache = true) {
        if (!ids.length) {
            return Promise.resolve([]);
        }

        if (!useCache) {
            return Promise.settle(ids.map(id => Tx.grab(id, false)))
                .then(ps => {
                    return ps.map(p => {
                        if (p.isFulfilled()) {
                            return p.value();
                        } else {
                            return null;
                        }
                    });
                });
        }

        var bag = _.zipObject(ids);

        if (helper.paramType(ids[0]) == helper.constant.ID_IDENTIFIER) {        // 传入的是 ID list
            return Promise.all(_.chunk(ids, 30000).map(args => sb.multi_get.apply(sb, args.map(id => `txid_${id}`))))
                .then(results => {       // 尝试获取 hash
                    results.forEach(result => {
                        _.extend(bag, _.chain(result).chunk(2).zipObject().mapKeys((v, k) => k.slice(5)).value());
                    });

                    let idList = _.keys(bag).filter(k => bag[k] == null);
                    let hashList = _.keys(bag).filter(k => bag[k] != null);

                    let idListPromises = Promise.settle(idList.map(id => Tx.grab(id, false)))
                        .then(idListPromisesResult => idListPromisesResult.map(p => p.isFulfilled() ? p.value() : null));

                    return Promise.join(Tx.multiGrab(hashList.map(id => bag[id])), idListPromises, (hashListResult, idListResult) => {

                        for (let i = 0, l = hashList.length; i < l; i++) {
                            bag[hashList[i]] = hashListResult[i];
                        }

                        for (let i = 0, l = idList.length; i < l; i++) {
                            bag[idList[i]] = idListResult[i];
                        }

                        return ids.map(id => bag[id]);
                    });
                });
        } else {        // 传入的是 hash list
            return sb.multi_get.apply(sb, ids.map(hash => `tx_${hash}`))
                .then(result => {
                    _.extend(bag, _.chain(result).chunk(2).zipObject().mapKeys((v, k) => k.slice(3)).mapValues(JSON.parse).value());

                    let hashList = _.keys(bag).filter(k => bag[k] == null);
                    return Promise.map(hashList, v => Tx.grab(v, false).catch(() => null))
                        .then(hashListResult => {
                            for (let i = 0, l = hashList.length; i < l; i++) {
                                bag[hashList[i]] = hashListResult[i];
                            }

                            return ids.map(id => bag[id]);
                        });
                });
        }
    }

    static getTableByHash(hash) {
        hash = String(hash);
        if (hash.length < 2) {
            return 'nonexists';
        }
        return sprintf('txs_%04d', parseInt(String(hash).slice(-2), 16) % 64);
    }

    static getTableById(id) {
        id = Number(id);
        if (isNaN(id) || id < 0) {
            return 'nonexists';
        }
        return sprintf('txs_%04d', Math.floor(id / 10e8));
    }

    getInputTable() {
        return sprintf('tx_inputs_%04d', this.attrs.tx_id % 100);
    }

    getOutputTable() {
        return sprintf('tx_outputs_%04d', this.attrs.tx_id % 100);
    }
}

module.exports = Tx;