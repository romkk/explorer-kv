var mysql = require('../lib/mysql');
var log = require('debug')('api:route:tx');
var Tx = require('../lib/tx');
var restify = require('restify');
var sb = require('../lib/ssdb')();
var helper = require('../lib/helper');

module.exports = (server) => {
    server.get('/rawtx/:txIdentifier', (req, res, next) => {
        Tx.grab(req.params.txIdentifier, !req.params.skipcache)
            .then(tx => {
                res.send(tx);
                next();
            }, () => {
                res.send(new restify.ResourceNotFoundError('Transaction not found'));
                next();
            });
    });
};
