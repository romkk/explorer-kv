var mysql = require('./mysql');
var Address = require('./address');
var moment = require('moment');
var log = require('debug')('api:AddressTxList');
var sb = require('./ssdb')();
var _ = require('lodash');
var assert = require('assert');

const END_OF_TIME = 1924963200;
const UNCONFIRMED_TABLE = 'address_txs_203001';

class AddressTxList {
    constructor(addr, ts = null, order = 'desc') {
        this._addr = addr;
        this._order = order;
        this._idx = 0;
        //当前实现中，如果用户指定了时间戳，则返回结果不会包含临时交易
        this._ts = ts == null ? (order == 'desc' ? END_OF_TIME : 0) : ts;
    }

    async slice(offset = 0, limit = 50) {
        var addrMapTableProp = `${this._order === 'desc' ? 'prev' : 'next'}_ymd`;
        var ret = [];
        if (this._addr.tx_count <= offset) {
            return ret;
        }

        //这里拆为临时表和非临时表进行查询，原因是 findfirsttable 查找表的逻辑不适用于临时表，且临时表不启用缓存
        //倒排且时间起点为结束点时，检查临时表是否满足需求
        if (this._order == 'desc' && this._ts == END_OF_TIME) {
            //let cnt = mysql.pluck(`select count(*) as cnt from address_txs_203001`, 'cnt');

            let sql = `SELECT * FROM ${UNCONFIRMED_TABLE}
                   WHERE address_id = ?
                   ORDER BY idx ${this._order == 'desc' ? 'desc' : 'asc'}
                   limit ?, ?`;
            let rows = await mysql.query(sql, [this._addr.id, offset, limit]);       // 直接limit，如果有性能问题，再参考 idx 做调整
            if (rows.length > 0) {
                offset = 0;
                limit -= rows.length;
            } else {
                offset -= await mysql.pluck(`select count(*) as cnt from ${UNCONFIRMED_TABLE} where address_id = ?`, 'cnt', [this._addr.id]);       //此时查询可能于前一条不一致，忽略
            }

            ret.push.apply(ret, rows);

            if (limit == 0) {       //implies offset == 0, can return result
                log(`address tx unconfirmed tx 满足需求, offset = ${offset}, limit = ${limit}`);
                return ret;
            }
        }

        // 检查常规表
        var table = await this.findFirstTable();

        // read table count cache from ssdb
        var cache = await sb.hgetall(`addr_table_${this._addr.address}`);
        cache = _.zipObject(_.chunk(cache, 2));

        while (true) {
            if (table == null) {
                return ret;
            }

            let cnt, prev, next;

            if (cache[table]) {
                log(`[cache hit] addr_table_${this._addr.address} ${table}`);
                [cnt, prev, next] = cache[table].split('|').map(Number);
            } else {
                log(`[cache miss] addr_table_${this._addr.address} ${table}`);
                //cache: addr_table_${address} => ${cnt}|${prev}|${next}
                let sqlPrev = `select idx, prev_ymd from ${table}
                               where address_id = ? order by idx asc limit 1`;
                let sqlNext = `select idx, next_ymd from ${table}
                               where address_id = ? order by idx desc limit 1`;
                let [prevRow, nextRow] = await* [
                    mysql.selectOne(sqlPrev, [this._addr.id]),
                    mysql.selectOne(sqlNext, [this._addr.id])
                ];

                cnt = nextRow.idx - prevRow.idx + 1;
                prev = prevRow.prev_ymd;
                next = nextRow.next_ymd;

                sb.hset(`addr_table_${this._addr.address}`, table, `${cnt}|${prev}|${next}`);
            }

            if (Number(cnt) <= offset) {    //单表无法满足 offset，直接下一张表
                log(`单表无法满足offset, cnt = ${cnt}, offset = ${offset}, limit = ${limit}`);
                offset -= cnt;
                this._idx = this._order == 'desc' ? this._idx - cnt : this._idx + cnt;

                table = Address.getAddressToTxTable(this._order === 'desc' ? prev : next);
                continue;
            }

            let sql = `select * from ${table}
                       where address_id = ? and idx between ? and ?
                       order by idx ${this._order}`;

            log(`单表 cnt = ${cnt}, offset = ${offset}, limit = ${limit}`);
            let idxStart, idxEnd;

            if (this._order == 'desc') {
                idxEnd = this._idx - offset;
                idxStart = idxEnd - limit + 1;
            } else {
                idxStart = this._idx + offset;
                idxEnd = idxStart + limit - 1;
            }

            let rows = await mysql.query(sql, [this._addr.id, idxStart, idxEnd]);
            ret.push.apply(ret, rows);
            if (cnt >= offset + limit) {    //单表即满足要求
                table = null;
            } else {    //单表不满足，需要继续下一张表
                limit -= rows.length;
                this._idx = this._order == 'desc' ? this._idx - offset - rows.length : this._idx + offset + rows.length;
                offset = 0;
                table = Address.getAddressToTxTable(rows[rows.length - 1][addrMapTableProp]);
            }
        }

    }

    async findHeight() {
        if (this._height == null) {
            var sql = `select height from 0_blocks
                       where \`curr_max_timestamp\` ${this._order === 'desc' ? '<=' : '>='} ?
                        and chain_id = 0
                       order by curr_max_timestamp ${this._order} limit 1`;

            this._height = await mysql.pluck(sql, 'height', [this._ts]);
        }

        return this._height;
    }

    //寻找查询开始的表，需要排除临时表
    async findFirstTable() {
        var specifiedDate = +moment.utc(this._ts * 1000).format('YYYYMMDD');
        var start = this._addr.begin_tx_ymd;

        if (start == 20300101) {      // 若开始为2030，则无已确认的交易
            return null;
        }

        var end = this._addr.last_confirmed_tx_ymd;

        //通过 max 和 min，排除临时表
        var table = Address.getAddressToTxTable(this._order === 'desc' ? Math.max(start, Math.min(end, specifiedDate)) : Math.min(end, Math.max(start, specifiedDate)));

        assert(table != UNCONFIRMED_TABLE, `表 ${table} 不应为临时表`);

        if (table == null) {        // 不存在
            return null;
        }

        var sql;

        var height = await this.findHeight();       //如果用户指定了非法的时间，则 height 可能为 null
        if (height == null) return null;

        //find the table and remember the idx corresponding to timestamp
        while (true) {
            sql = `select idx
                   from ${table}
                   where address_id = ? and tx_height ${this._order === 'desc' ? '<=' : '>='} ?
                   order by tx_height ${this._order} limit 1`;

            try {
                this._idx = await mysql.pluck(sql, 'idx', [this._addr.id, height]);
                if (this._idx == null) {        //尝试的当前表没有有效的条目
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

        let pull = async () => {
            if (table == null) {
                return false;
            }

            var sql = `select * from ${table}
                       where address_id = ? and tx_height ${this._order === 'desc' ? '<=' : '>='} ?
                       order by idx ${this._order}
                       limit ?, ?`;
            cache = await mysql.query(sql, [ this._addr.id, height, offset, PAGESIZE ]);

            if (!cache.length) return false;

            var prop = this._order === 'desc' ? 'prev_ymd' : 'next_ymd';
            var nextYmd = _.last(cache)[prop];
            var nextTable = Address.getAddressToTxTable(nextYmd);

            if (nextTable == table) {
                offset += PAGESIZE;
            } else {
                offset = 0;
            }

            table = nextTable;

            return true;
        };

        async function next() {
            if (!cache.length) {
                if (!(await pull())) {  // no more txs
                    return null;
                }
            }

            return cache.shift();
        }

        return next;
    }
}

AddressTxList.make = async (addr, ts, order) => {
    var a = await Address.grab(addr);
    return new AddressTxList(a, ts, order);
};

module.exports = AddressTxList;