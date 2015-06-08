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
    /*
    server.get('/unspent', async (req, res, next) => {
        var err = validators.isValidAddressList(req.query.active);
        if (err != null) {
            return next(err);
        }

        var parts = req.params.active.trim().split('|');
        var ret = _.zipObject(parts);

        var height = await Block.getLatestHeight();

        await* parts.map(async p => {
            var addr = await Address.grab(p, !req.params.skipcache);
            if (addr == null) {
                return [];
            }

            var table = sprintf('address_unspent_outputs_%04d', addr.attrs.id % 10);
            var sql = `select tx_id, block_height, value
                       from ${table}
                       where address_id = ?
                       order by tx_id asc, position asc, position2 asc`;

            var rows = await mysql.query(sql, [addr.attrs.id]);

            if (rows.length === 0) {
                return [];
            }

            ret[p] = rows.map(r => ({
                tx_index: r.tx_id,
                value: r.value,
                value_hex: r.value.toString(16),
                confirmations: height - r.block_height + 1
            }));
        });

        res.send(ret);
        next();
    });
    */

    server.get('/unconfirmed-transactions', (req, res, next) => {
        const PAGESIZE = 1000;

        var sql = `show tables like 'txlogs_%'`;
        mysql.query(sql)
            .then(rows => {
                if (rows.length == 0) {
                    console.error('Txlogs Table not found !');
                    throw new restify.ServiceUnavailableError('Service unavailable, please try later.');
                }
                return Object.values(rows[rows.length - 1])[0];
            })
            .then(table => {
                var offset = 0;
                var txs = [];
                return new Promise((resolve, reject) => {
                    (function loop() {
                        var sql = `select id, tx_hash, handle_type, block_height
                                   from ${table}
                                   order by id desc
                                   limit ${offset}, ${PAGESIZE}`;
                        mysql.query(sql).then(rows => {
                            for (let r of rows) {
                                if (r.handle_type == 1 && r.block_height == -1) {
                                    txs.push(r.tx_hash);
                                } else {
                                    return resolve(txs.reverse());
                                }
                            }

                            offset += PAGESIZE;
                            loop();
                        });
                    })();
                });
            })
            .then(txHashs => {
                return Promise.all(txHashs.map(hash => {
                    return Tx.make(hash).then(tx => {
                        if (tx == null) {
                            return tx;
                        }
                        return tx.load();
                    });
                }));
            })
            .then(txs => {
                res.send({
                    txs: _.compact(txs)
                });
                next();
            })
            .catch(err => {
                next(err);
            });
    });
};