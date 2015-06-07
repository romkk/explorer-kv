var mysql = require('./mysql');
var helper = require('./helper');
var sprintf = require('sprintf').sprintf;
var log = require('debug')('api:lib:block');
var moment = require('moment');
var assert = require('assert');
var BN = require('bn.js');
var Tx = require('./tx');
var sb = require('../lib/ssdb')();
var _ = require('lodash');

class Address {
    constructor(row) {
        this.attrs = row;
    }

    get txs() {
        if (this._txs == null) {
            throw new Error('Address instance has not been loaded');
        }
        return this._txs;
    }

    set txs(v) {
        this._txs = v;
    }

    toJSON() {
        return {
            hash160: helper.addressToHash160(this.attrs.address),
            address: this.attrs.address,
            n_tx: this.attrs.tx_count,
            n_unredeemed: null,
            total_received: this.attrs.total_received,
            total_sent: this.attrs.total_sent,
            final_balance: (this.attrs.total_received - this.attrs.total_sent),
            txs: this.txs
        };
    }

    //TODO： 使用 cache 优化
    load(timestamp = null, order = 'desc', offset = 0, limit = 50) {
        order = order === 'desc' ? 'desc' : 'asc';
        if (timestamp == null) {
            timestamp = order === 'desc' ? moment.utc().unix() : 0;
        }

        var addrMapTableProp = `${order === 'desc' ? 'prev' : 'next'}_ymd`;

        var ret = [];

        return this.getStartTable(timestamp, order)
            .then((table) => {
                return new Promise(resolve => {
                    if (this.attrs.tx_count <= offset) {
                        return resolve(ret);
                    }

                    var loop = () => {

                        if (table == null) {    //没有更多了
                            return resolve(ret);
                        }

                        var sqlRowCnt = `select count(id) as cnt from ${table}
                                 where address_id = ?`;
                        mysql.pluck(sqlRowCnt, 'cnt', [this.attrs.id])
                            .then((cnt) => {
                                if (cnt <= offset) {        //单表无法满足 offset，直接下一张表
                                    log(`单表无法满足offset, cnt = ${cnt}, offset = ${offset}, limit = ${limit}`);
                                    offset -= cnt;

                                    let tmpOrder = order === 'desc' ? 'asc' : 'desc';

                                    var sqlNextTable = `select ${addrMapTableProp} as next from ${table}
                                    where address_id = ?
                                    order by tx_height ${tmpOrder}, id ${tmpOrder}
                                    limit 1`;

                                    return mysql.pluck(sqlNextTable, 'next', [this.attrs.id])
                                        .then(next => {
                                            table = this.getAddressToTxTable(next);
                                            return loop();
                                        });
                                }

                                var sql = `select * from ${table}
                                    where address_id = ?
                                    order by idx ${order}
                                    limit ?, ?`;

                                log(`单表 cnt = ${cnt}, offset = ${offset}, limit = ${limit}`);
                                return mysql.query(sql, [this.attrs.id, offset, limit])
                                    .then(rows => {

                                        ret.push.apply(ret, rows);

                                        if (cnt >= offset + limit) {    //单表即满足要求
                                            table = null;
                                        } else {    //单表不满足，需要继续下一张表
                                            limit -= rows.length;
                                            offset = 0;
                                            table = this.getAddressToTxTable(rows[rows.length - 1][addrMapTableProp]);
                                        }
                                        return loop();
                                    });
                            });
                    };

                    loop();
                });
            })
            .then(rows => {
                return Promise.all(rows.map(r => {
                    return Tx.make(r.tx_id).then(tx => tx.load());
                }))
            }).then(txs => {
                this.txs = txs;
                return this;
            });
    }

    getStartTable(timestamp, order) {
        var date = +moment.utc(timestamp * 1000).format('YYYYMMDD');
        var end = this.attrs.end_tx_ymd;
        var start = this.attrs.begin_tx_ymd;
        var table = this.getAddressToTxTable(order === 'desc' ? Math.min(end, date) : Math.max(start, date));

        // 调用优化后的 height 获取方法
        var sql = `select height from 0_blocks where \`timestamp\` ${order === 'desc' ? '<=' : '>='} ?
                   order by block_id ${order} limit 1`;

        return mysql.pluck(sql, 'height', [timestamp])
            .then(height => {
                return new Promise((resolve, reject) => {
                    var loop = () => {
                        var sql = `select id
                               from ${table}
                               where address_id = ? and tx_height ${order === 'desc' ? '<=' : '>='} ?
                               order by tx_height ${order} limit 1`;
                        mysql.pluck(sql, 'id', [this.attrs.id, height])
                            .then(id => {
                                if (id === null) {
                                    let postfix = +table.slice(-6);
                                    let newPostfix = moment.utc(postfix, 'YYYYMM');
                                    newPostfix = (order === 'desc' ? newPostfix.subtract(1, 'months') : newPostfix.add(1, 'months')).format('YYYYMM');
                                    table = table.slice(0, -6) + newPostfix;
                                    return loop();
                                }

                                return resolve(table);
                            }, err => {
                                if (err.code === 'ER_NO_SUCH_TABLE') {
                                    return resolve(null);
                                } else {
                                    return reject(err);
                                }
                            })

                    };

                    loop();
                });
            });
    }

    static make(addr) {
        var table = Address.getTableByAddr(addr);
        var sql = `select *
                   from ${table}
                   where address = ? limit 1`;
        return mysql.selectOne(sql, [addr])
            .then(row => {
                if (row == null) {
                    return null;
                }

                sb.set(`addr_${addr}`, JSON.stringify(row));

                return new Address(row);
            });
    }

    getAddressToTxTable(date) {
        date = +date;

        if (date === 0) {
            return null;
        }

        return sprintf('address_txs_%d', Math.floor(date / 100));
    }

    static getTableByAddr(addr) {
        return sprintf('addresses_%04d', parseInt(helper.addressToHash160(addr).slice(-2), 16) % 64);
    }

}

Address.grab = async (addr, useCache = true) => {
    if (useCache) {
        let def = await sb.get(`addr_${addr}`);
        if (def == null) {
            return await Address.make(addr);
        }
        return new Address(JSON.parse(def));
    } else {
        return await Address.make(addr);
    }
};

Address.multiGrab = async (addrs, useCache = true) => {
    if (!useCache) {
        return await* addrs.map(addr => Address.grab(addr, false));
    }

    var bag = _.zipObject(addrs);
    var defs = await sb.multi_get.apply(sb, addrs.map(addr => `addr_${addr}`));
    _.extend(bag, _.chain(defs).chunk(2).zipObject().mapKeys((v, k) => k.slice(5)).mapValues(v => JSON.parse(v)).value());
    var missedAddrs = addrs.filter(addr => bag[addr] == null);
    var missedAddrDefs = await* missedAddrs.map(addr => Address.grab(addr));
    _.extend(bag, _.zipObject(missedAddrs, missedAddrDefs));

    return addrs.map(addr => bag[addr] == null ? null : new Address(bag[addr]));
};

module.exports = Address;