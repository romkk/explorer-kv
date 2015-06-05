var mysql = require('../lib/mysql');
var log = require('debug')('api:route:address');
var helper = require('../lib/helper');
var restify = require('restify');
var _ = require('lodash');
var Tx = require('../lib/tx');
var Address = require('../lib/address');
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
        var ret = _.zipObject(parts);

        //TODO：修改为get latest block
        var sql = `select height from 0_blocks
                   where chain_id = 0
                   order by block_id desc
                   limit 1`;

        mysql.pluck(sql, 'height')
            .then(height => {
                return Promise.map(parts, function(p) {
                    var unspentList = [];
                    return Address.make(p)
                        .then(addr => {
                            if (addr == null) {
                                return [];
                            }

                            var table = sprintf('address_unspent_outputs_%04d', addr.attrs.id % 10);
                            var sql = `select tx_id, block_height, value
                                       from ${table}
                                       where address_id = ?
                                       order by tx_id asc, position asc, position2 asc`;
                            return mysql.query(sql, [addr.attrs.id]);
                        })
                        .then(rows => {
                            if (rows.length == 0) {
                                return [];
                            }

                            unspentList = rows.map(r => ({
                                tx_index: r.tx_id,
                                value: r.value,
                                value_hex: r.value.toString(16),
                                confirmations: height - r.block_height + 1
                            }));

                            return Promise.map(rows, r => Tx.make(r.tx_id));
                        })
                        .then(txs => {
                            for (let i = 0, l = unspentList.length; i < l; i++) {
                                unspentList[i].tx_hash = txs[i].attrs.hash;
                                unspentList[i].tx_hash_big_endian = helper.toBigEndian(txs[i].attrs.hash);
                                unspentList[i].tx_output_n = txs[i].attrs.output_count;
                            }

                            ret[p] = unspentList;
                        });
                }).return(ret);
            })
            .then(ret => {
                res.send(ret);
                next();
            }, next);
    });

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