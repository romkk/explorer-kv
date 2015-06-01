var mysql = require('./mysql');
var helper = require('./helper');
var sprintf = require('sprintf').sprintf;
var log = require('debug')('api:lib:block');
var moment = require('moment');
var assert = require('assert');
var BN = require('bn.js');
var Tx = require('./tx');

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

    load(order = 'desc', offset = 0, limit = 50) {
        order = order === 'desc' ? 'desc' : 'asc';
        var addrMapTableProp = `${order === 'desc' ? 'prev' : 'next'}_ymd`;

        return new Promise((resolve, reject) => {
            var ret = [], table = this.getStartTable(order);

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
                                    order by tx_height ${order}, id ${order}
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

    getStartTable(order) {      //修改为异步获取，支持 offset 起点优化
        return this.getAddressToTxTable(this.attrs[`${order === 'desc' ? 'end' : 'begin'}_tx_ymd`]);
    }

    static make(addr) {
        var table = Address.getTableByAddr(addr);
        var sql = `select *
                   from ${table}
                   where address = ?`;
        return mysql.selectOne(sql, [addr])
            .then(row => {
                return row == null ? null : new Address(row);
            });
    }

    getAddressToTxTable(date) {
        if (date === 0) {
            return null;
        }
        return sprintf('address_txs_%d', Math.floor(date / 100));
    }

    static getTableByAddr(addr) {
        return sprintf('addresses_%04d', parseInt(helper.addressToHash160(addr).slice(-2), 16) % 64);
    }

}

module.exports = Address;