let mysql = require('./mysql');
let log = require('debug')('api:lib:api');
let restify = require('restify');
let Block = require('./block');
let _ = require('lodash');

let poolDefCache = null;

async function getPoolDefinition(useCache) {
    if (useCache && poolDefCache) {
        return poolDefCache;
    }

    let rows = await mysql.query('select pool_id, name, key_words, coinbase_address from 0_pool');
    var bag = {};

    rows.forEach(r => {
        if (!bag[r.name]) bag[r.name] = [];
        try {
            bag[r.name] = {
                id: r.pool_id,
                re: _.isEmpty(r.key_words) ? /^(\u0000)+$/ : new RegExp(r.key_words, 'i'),  //任何 coinbase 字符串都不可能全部是 0
                address: r.coinbase_address.split('|')
            };
        } catch (err) {
            log(`[WARN] ${r.name} 正则表达式或地址初始化失败，请检查。`);
        }
    });

    return poolDefCache = bag;
}

module.exports = {
    async identifyBlock(hash, useCache = true) {
        var blk;
        try {
            blk = await Block.grab(hash, 0, 1, true, !useCache);
        } catch (err) {
            return new restify.ResourceNotFoundError('Block Not Found');
        }

        if (!blk.tx.length) {
            return new restify.ServiceUnavailableError('Block Not Available');
        }

        var [tx] = blk.tx;
        var text;
        try {
            text = new Buffer(tx.inputs[0].script, 'hex').toString('utf8');
        } catch (err) {
            return next(err);
        }
        var addr = tx.out[0].addr[0];

        let poolDef = await getPoolDefinition(useCache);

        // 查找矿池
        var pool = _.findKey(poolDef, v => v.re.test(text) || v.address.includes(addr));

        return {
            blk: blk,
            name: pool || 'Unknown',
            id: pool ? poolDef[pool].id : 0
        };
    }
};