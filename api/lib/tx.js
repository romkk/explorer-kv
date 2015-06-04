var mysql = require('./mysql');
var helper = require('./helper');
var sprintf = require('sprintf').sprintf;
var log = require('debug')('api:lib:tx');
var moment = require('moment');
var sb = require('./ssdb')();

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
            relayed_by: null,
            out: this.outputs,
            lock_time: this.attrs.lock_time,
            size: this.attrs.size,
            time: this.attrs.block_timestamp,
            tx_index: this.attrs.tx_id,
            hash: this.attrs.hash,
            vin_sz: this.attrs.inputs_count,
            vout_sz: this.attrs.outputs_count,
            is_coinbase: !!this.attrs.is_coinbase
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
        sql = `SELECT id, tx_id, position, input_script_hex, sequence,
                        prev_tx_id, prev_position, prev_value, prev_address,
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
                    if (!this.attrs.is_coinbase) {
                        ret.prev_out = {        //omit `spent` and `script`
                            tx_index: r.prev_tx_id,
                            //type: null,     //tbd
                            addr: r.prev_address.split('|'),
                            value: r.prev_value,
                            n: r.prev_position
                        };
                    }
                    return ret;
                });
            });


        // 获取 outputs
        sql = `SELECT tx_id, position, address, address_ids, value, output_script_hex, spent_tx_id
                FROM \`${this.getOutputTable()}\`
                WHERE \`tx_id\` = ?
                ORDER BY position asc`;
        var outputPromise = mysql.query(sql, [this.attrs.tx_id])
            .then((rows) => {
                this.outputs = rows.map(r => {
                    let ret = {};
                    ret.spent = r.spent_tx_id != 0;
                    ret.tx_index = r.tx_id;
                    //ret.type = null;
                    ret.addr = r.address.split('|');
                    ret.value = r.value;
                    ret.n = r.position;
                    ret.script = r.output_script_hex;

                    if (this.attrs.is_coinbase) {
                        ret.addr_tag_link = null;
                        ret.addr_tag = null;
                    }
                    return ret;
                });
            });

        return Promise.all([inputPromise, outputPromise])
            .then(() => {
                log(`set cache tx_${this.attrs.tx_id}`);
                sb.multi_set(
                    `tx_${this.attrs.tx_id}`, JSON.stringify(this),
                    `txhash_${this.attrs.hash}`, this.attrs.tx_id
                );

                return this;
            });
    }

    static make(id) {
        var idType = helper.paramType(id);
        var table = idType == helper.constant.HASH_IDENTIFIER ? Tx.getTableByHash(id) : Tx.getTableById(id);
        var sql = `select tx_id, hash, height, block_timestamp, is_coinbase,
                   version, lock_time, size, fee, total_in_value,
                   total_out_value, inputs_count, outputs_count, created_at
                   from ${table}
                   where ${idType == helper.constant.HASH_IDENTIFIER ? `hash = ?` : `tx_id = ?`}`;
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
            if (idType == helper.constant.HASH_IDENTIFIER) {
                p = p.then(() => sb.get(`txhash_${id}`))
                    .then(v => {
                        if (v == null) {
                            log(`[cache miss] tx_query = ${id}`);
                            return Promise.reject();    //查无此 hash，即缓存内不存在
                        }
                        return v;
                    });
            }

            p = p.then(realId => sb.get(`tx_${realId}`))
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

        var bag = {};
        var omittedIds = [];

        return sb.multi_get.apply(sb, ids.map(id => `tx_${id}`))
            .then(result => {
                for (let i = 0, l = result.length; i < l; i += 2) {
                    bag[result[i].slice(3)] = result[i + 1];
                }

                var ps = [];

                for (let id of ids) {
                    if (bag[id] == null) {
                        ps.push(Tx.grab(id, false));
                        omittedIds.push(id);
                    }
                }

                return Promise.settle(ps);
            })
            .then(txPromises => {
                for (let i = 0, l = txPromises.length; i < l; i++) {
                    let txp = txPromises[i];
                    if (txp.isFulfilled()) {
                        bag[omittedIds[i]] = txp.value();
                    } else {
                        bag[omittedIds[i]] = null;
                    }
                }

                return ids.map(id => {
                    return JSON.parse(bag[id]);
                });
            });
    }

    static getTableByHash(hash) {
        return sprintf('txs_%04d', parseInt(String(hash).slice(-2), 16) % 64);
    }

    static getTableById(id) {
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