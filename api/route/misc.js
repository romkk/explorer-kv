var mysql = require('../lib/mysql');
var log = require('debug')('api:route:address');
var helper = require('../lib/helper');
var restify = require('restify');
var _ = require('lodash');
var Tx = require('../lib/Tx');

module.exports = (server) => {
    server.get('/unspent', (req, res, next) => {

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