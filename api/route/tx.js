var mysql = require('../lib/mysql');
var sprintf = require('sprintf').sprintf;
var log = require('debug')('api:route:tx');

/**
 * Get block detail.
 *
 * @URL /rawblock
 *
 */

module.exports = (server) => {
    server.get('/rawtx/:txIdentifier', (req, res, next) => {
        res.send(req.params);
        next();
    });
};
