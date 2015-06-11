var mysql = require('./mysql');
var helper = require('./helper');
var sprintf = require('sprintf').sprintf;
var log = require('debug')('api:lib:block');
var Tx = require('./tx');
var sb = require('./ssdb')();
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

    static getAddressToTxTable(date) {
        if (date == null) {
            throw new Error(`date can not be undefined, date = ${date}`);
        }

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
    var missedAddrDefs = await * missedAddrs.map(addr => Address.grab(addr));
    _.extend(bag, _.zipObject(missedAddrs, missedAddrDefs));

    return addrs.map(addr => bag[addr] == null ? null : new Address(bag[addr]));
};

module.exports = Address;