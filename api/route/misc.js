var mysql = require('../lib/mysql');
var log = require('debug')('api:route:address');
var helper = require('../lib/helper');
var restify = require('restify');
var _ = require('lodash');
var Tx = require('../lib/Tx');
var Address = require('../lib/Address');
var validators = require('../lib/custom_validators');
var sprintf = require('sprintf').sprintf;
var assert = require('assert');

module.exports = (server) => {
    server.get('/unspent', (req, res, next) => {
        var err = validators.isValidAddressList(req.query.active);
        if (err != null) {
            return next(err);
        }

        var parts = req.params.active.trim().split('|');

        var sql = `select height from 0_blocks
                   where chain_id = 0
                   order by block_id desc
                   limit 1`;

        mysql.pluck(sql, 'height')
            .then(height => {
                return Promise.all(parts.map(p => {
                    var ret = {};
                    return Address.make(p)
                        .then(addr => {
                            if (addr == null) {
                                throw new restify.InvalidArgumentError(`Address not found: ${p}`);
                            }
                            var table = sprintf('address_unspent_outputs_%04d', addr.attrs.id % 10);
                            var sql = `select tx_id, block_height, value
                                 from ${table}
                                 where address_id = ?`;
                            return mysql.selectOne(sql, [addr.attrs.id]);
                        })
                        .then(row => {
                            assert(row !== null, 'row can not be null');
                            ret.value = row.value;
                            ret.value_hex = row.value.toString(16);
                            ret.confirmations = height - row.block_height + 1;

                            // get tx
                            return Tx.make(row.tx_id);
                        })
                        .then(tx => {
                            assert(tx !== null, 'tx can not be null');
                            ret.tx_hash = tx.attrs.hash;
                            ret.tx_hash_big_endian = helper.toBigEndian(tx.attrs.hash);
                            ret.tx_index = tx.attrs.tx_id;
                            ret.tx_output_n = tx.attrs.outputs_count;

                            return ret;
                        });
                }));
            })
            .then(ret => {
                res.send(ret);
                next();
            }, next);
    });

    server.get('/unconfirmed-transactions', (req, res, next) => {
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
                                   limit ${offset}, 100`;
                        mysql.query(sql).then(rows => {
                            for (let r of rows) {
                                if (r.handle_type == 1 && r.block_height == -1) {
                                    txs.push(r.tx_hash);
                                } else {
                                    return resolve(txs.reverse());
                                }
                            }

                            offset += 100;
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