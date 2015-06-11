var mysql = require('./mysql');
var Address = require('./address');
var moment = require('moment');
var log = require('debug')('api:AddressTxList');
var sb = require('./ssdb')();
var _ = require('lodash');

class AddressTxList {

    constructor(addr, ts = moment().unix(), order = 'desc') {
        this._addr = addr;
        this._ts = ts;
        this._order = order;
    }

    async slice(offset = 0, limit = 50) {
        var addrMapTableProp = `${this._order === 'desc' ? 'prev' : 'next'}_ymd`;

        var ret = [];
        var table = await this.findFirstTable(this._ts, this._order);

        if (this._addr.tx_count <= offset) {
            return ret;
        }

        // read table count cache from ssdb
        var cache = await sb.hgetall(`addr_table_${this._addr.address}`);
        cache = _.zipObject(_.chunk(cache, 2));

        while (true) {
            if (table == null) {
                return ret;
            }

            let cnt;

            if (cache[table]) {
                log(`[cache hit] addr_table_${this._addr.address} ${table}`);
                cnt = cache[table];
            } else {
                log(`[cache miss] addr_table_${this._addr.address} ${table}`);
                let sqlRowCnt = `select count(id) as cnt from ${table}
                                 where address_id = ?`;
                cnt = await mysql.pluck(sqlRowCnt, 'cnt', [this._addr.id]);
                sb.hset(`addr_table_${this._addr.address}`, table, cnt);
            }

            if (Number(cnt) <= offset) {    //单表无法满足 offset，直接下一张表
                log(`单表无法满足offset, cnt = ${cnt}, offset = ${offset}, limit = ${limit}`);
                offset -= cnt;

                let tmpOrder = this._order === 'desc' ? 'asc' : 'desc';

                let sqlNextTable = `select ${addrMapTableProp} as next from ${table}
                                    where address_id = ?
                                    order by tx_height ${tmpOrder}, id ${tmpOrder}
                                    limit 1`;

                let next = await mysql.pluck(sqlNextTable, 'next', [this._addr.id]);
                table = Address.getAddressToTxTable(next);
                continue;
            }

            let sql = `select * from ${table}
                                    where address_id = ? and tx_height ${this._order === 'desc' ? '<=' : '>='} ?
                                    order by idx ${this._order}
                                    limit ?, ?`;

            log(`单表 cnt = ${cnt}, offset = ${offset}, limit = ${limit}`);
            let rows = await mysql.query(sql, [this._addr.id, await this.findHeight(), offset, limit]);
            ret.push.apply(ret, rows);
            if (cnt >= offset + limit) {    //单表即满足要求
                table = null;
            } else {    //单表不满足，需要继续下一张表
                limit -= rows.length;
                offset = 0;
                table = Address.getAddressToTxTable(rows[rows.length - 1][addrMapTableProp]);
            }
        }

    }

    async findHeight() {
        if (this._height == null) {
            var sql = `select height from 0_blocks where \`timestamp\` ${this._order === 'desc' ? '<=' : '>='} ?
                   order by block_id ${this._order} limit 1`;

            this._height = await mysql.pluck(sql, 'height', [this._ts]);
        }

        return this._height;
    }

    async findFirstTable() {
        var date = +moment.utc(this._ts * 1000).format('YYYYMMDD');
        var end = this._addr.end_tx_ymd;
        var start = this._addr.begin_tx_ymd;
        var table = Address.getAddressToTxTable(this._order === 'desc' ? Math.min(end, date) : Math.max(start, date));
        var sql, id;

        var height = await this.findHeight();

        while (true) {
            sql = `select id
                   from ${table}
                   where address_id = ? and tx_height ${this._order === 'desc' ? '<=' : '>='} ?
                   order by tx_height ${this._order} limit 1`;

            try {
                id = await mysql.pluck(sql, 'id', [this._addr.id, height]);
                if (id == null) {
                    let postfix = +table.slice(-6);
                    let newPostfix = moment.utc(postfix, 'YYYYMM');
                    newPostfix = (this._order === 'desc' ? newPostfix.subtract(1, 'months') : newPostfix.add(1, 'months')).format('YYYYMM');
                    table = table.slice(0, -6) + newPostfix;
                    continue;
                }
                return table;
            } catch (err) {
                if (err.code === 'ER_NO_SUCH_TABLE') {
                    return null;
                } else {
                    throw err;
                }
            }
        }

    }

    async iter() {
        var cache = [];
        var table = await this.findFirstTable();
        var height = await this.findHeight();
        var offset = 0;
        const PAGESIZE = 100;

        var pull = () => {
            if (table == null) {
                return false;
            }

            var sql = `select * from ${table}
                       where address_id = ? and tx_height ${this._order === 'desc' ? '<=' : '>='} ?
                       order by idx ${this._order}
                       limit ?, ?`;
            return mysql.query(sql, [ this._addr.id, height, offset, PAGESIZE])
                .then(rows => {
                    cache = rows;

                    var last = cache[cache.length - 1];
                    var prop = this._order === 'desc' ? 'prev_ymd' : 'next_ymd';
                    var nextYmd = last[prop];
                    var nextTable = Address.getAddressToTxTable(nextYmd);

                    if (nextTable == table) {
                        offset += PAGESIZE;
                    } else {
                        offset = 0;
                    }

                    table = nextTable;

                    return true;
                });
        };

        log(`start: cache.length = ${cache.length}`);

        var gen = function* () {
            let v = Promise.resolve();
            var done = false;

            while (!done) {

                log(`${Date.now()}: cache.length = ${cache.length}`);

                if (!cache.length) {
                    v = v.then(pull)
                        .then(success => {
                            if (done = !success) {
                                throw new Error(`${this._addr.address}: No more rows`);
                            }
                        });
                }
                log('yield');
                yield v.then(() => {
                    return cache.shift();
                });

            }
        }.bind(this);

        return gen();
    }
}

AddressTxList.make = async (addr, ts, order) => {
    var a = await Address.grab(addr);
    return new AddressTxList(a, ts, order);
};

module.exports = AddressTxList;