var mysql = require('../lib/mysql');
var log = require('debug')('api:route:tx');
var Tx = require('../lib/tx');
var restify = require('restify');
var sb = require('../lib/ssdb')();
var helper = require('../lib/helper');

module.exports = (server) => {
    server.get('/rawtx/:txIdentifier', async (req, res, next) => {
        var ids = req.params.txIdentifier.split(',');
        var txs = await Tx.multiGrab(ids, !req.params.skipcache);

        if (ids.length == 1) {
            let [tx] = txs;
            if (tx == null) {
                res.send(new restify.ResourceNotFoundError('Transaction not found'));
            } else {
                res.send(tx);
            }
        } else {
            res.send(txs);
        }

        next();
    });
};
