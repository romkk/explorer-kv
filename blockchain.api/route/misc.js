var mysql = require('../lib/mysql');
var log = require('debug')('api:route:misc');
var sprintf = require('sprintf').sprintf;
var helper = require('../lib/helper');
var restify = require('restify');
var _ = require('lodash');
var Tx = require('../lib/tx');
var Address = require('../lib/address');
var Block = require('../lib/block');
var validators = require('../lib/custom_validators');

module.exports = (server) => {
    server.get('/unconfirmed-transactions', async (req, res, next) => {
        var txHashList = await mysql.list(`select tx_hash from 0_unconfirmed_txs order by position asc`, ['tx_hash']);
        var txs = await Tx.multiGrab(txHashList, !req.params.skipcache);
        res.send(_.compact(txs));
        next();
    });

    server.get('/identify-block/:hash', async (req, res, next) => {
        let rows = await mysql.query('select pool_id, name, key_words from 0_pool');
        var bag = {};
        rows.forEach(r => {
            if (!bag[r.name]) bag[r.name] = [];
            try {
                bag[r.name] = {
                    re: new RegExp(r.key_words, 'i'),
                    id: r.pool_id
                }
            } catch (err) {
                log(`[WARN] ${r.name} 正则表达式初始化失败，请检查。`);
            }

        });

        var hash = req.params.hash;

        var blk;
        try {
            blk = await Block.grab(hash, 0, 1, true, !req.params.skipcache);
        } catch (err) {
            return next(new restify.ResourceNotFoundError('Block Not Found'));
        }

        if (!blk.tx.length) {
            return next(new restify.ServiceUnavailableError('Block Not Available'));
        }

        var [tx] = blk.tx;
        var text = new Buffer(tx.inputs[0].script, 'hex').toString('utf8');

        var pool = _.findKey(bag, v => v.re.test(text));
        var id = pool ? bag[pool].id : 0;
        var name = pool || 'Unknown';

        mysql.query(`update 0_blocks set relayed_by = ? where hash = ?`, [id, blk.hash]);

        res.send({
            relayedBy: name
        });
        next();
    });

    server.get('/unspent', async (req, res, next) => {
        var err = validators.isValidAddressList(req.query.active);
        if (err != null) {
            return next(err);
        }

        var parts = req.params.active.trim().split('|');
        var ret = [];

        var [height, addrs] = await* [Block.getLatestHeight(), Address.multiGrab(parts, !req.params.skipcache)];

        while (ret.length < 200 && addrs.length) {
            let addr = addrs.shift();
            if (addr == null) continue;
            let table = sprintf('address_unspent_outputs_%04d', addr.attrs.id % 10);
            let sql = `select tx_id, block_height, value, position
                       from ${table}
                       where address_id = ?
                       order by block_height asc, position asc`;

            let rows = await mysql.query(sql, [addr.attrs.id]);

            if (!rows.length) continue;

            let txs = await Tx.multiGrab(rows.map(r => r.tx_id), !req.params.skipcache);

            rows.every((r, i) => {
                ret.push({
                    tx_hash: txs[i].hash,
                    tx_index: r.tx_id,
                    tx_output_n: r.position,
                    script: txs[i].out[r.position].script,
                    value: r.value,
                    value_hex: r.value.toString(16),
                    confirmations: height - r.block_height + 1
                });
                return ret.length < 200;
            });
        }

        res.send(ret);
        next();
    });
};