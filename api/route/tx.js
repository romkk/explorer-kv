var mysql = require('../lib/mysql');
var log = require('debug')('api:route:tx');
var Tx = require('../lib/tx');
var restify = require('restify');

module.exports = (server) => {
    server.get('/rawtx/:txIdentifier', (req, res, next) => {
        Tx.make(req.params.txIdentifier)
            .then(tx => {
                if (tx == null) {
                    return new restify.ResourceNotFoundError('Transaction not found');
                }
                return tx.load(req.query.scripts === 'true');
            })
            .then((tx) => {
                res.send(tx);
                next();
            });
    });
};
