var mysql = require('../lib/mysql');
var sprintf = require('sprintf').sprintf;
var log = require('debug')('api:route:block');

/**
 * Get block detail.
 *
 * @URL /block-
 *
 */

module.exports = (server) => {
    server.get('/rawblock/:blockIdentifier', (req, res, next) => {
        res.send(req.params);
        next();
    });
};
