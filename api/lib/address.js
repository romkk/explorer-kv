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
        var addrTableProp = `${order === 'desc' ? 'end' : 'begin'}_tx_ymd`;
        var addrMapTableProp = `${order === 'desc' ? 'prev' : 'next'}_ymd`;

        return new Promise((resolve, reject) => {
            var ret = [],
                fetched = 0, offsetOverload = false,
                table = this.getAddressToTxTable(this.attrs[addrTableProp]);

            var loop = function() {
                if (table == null) {        //没有更多了
                    return resolve(ret);
                }

                var sql = `select *
                   from ${table}
                   where address_id = ?
                   order by tx_height ${order}, id ${order}
                   limit ?`;

                mysql.query(sql, [this.attrs.id, limit - ret.length])
                    .then(rows => {
                        assert(rows.length > 0, 'AddressTx 表为空');

                        fetched += rows.length;

                        console.log('table: ', rows[rows.length - 1][addrMapTableProp], rows[rows.length - 1]);

                        table = this.getAddressToTxTable(rows[rows.length - 1][addrMapTableProp]);

                        if (fetched <= offset) {
                            return loop();
                        }

                        /**
                         *             start = offset - fetched
                         *               |
                         *  .  .  . [ .  .  .  .  . ][ .  .  .  . ]
                         *            |           |             |
                         *            |           |             |
                         *          offset     fetched   overloaded, push all
                         */
                        ret.push.apply(ret, offsetOverload ? rows : rows.slice(offset - fetched));
                        offsetOverload = true;

                        if (ret.length < limit) {
                            return loop();
                        } else {
                            return resolve(ret);
                        }
                    });
            }.bind(this);

            loop();
        }).then(rows => {
            return Promise.all(rows.map(r => {
                return Tx.make(r.tx_id).then(tx => tx.load());
            }))
        }).then(txs => {
            this.txs = txs;
            return this;
        });
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
        return sprintf('address_txs_%04d', date);
    }

    static getTableByAddr(addr) {
        return sprintf('addresses_%04d', parseInt(helper.addressToHash160(addr).slice(-2), 16) % 64);
    }

}

module.exports = Address;