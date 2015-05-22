var mysql = require('../lib/mysql');
var Block = require('../lib/block');
var log = require('debug')('api:route:block');

/**
 * Get block detail.
 *
 * @URL /block-
 *
 */

module.exports = (server) => {
    server.get('/rawblock/:blockIdentifier', (req, res, next) => {
        Block.make(req.params.blockIdentifier)
            .then(blk => {
                if (blk == null) {
                    return next(new restify.ResourceNotFoundError('Block not found'));
                }
                return blk;
            })
            .then(blk => {
                res.send(blk);
                next();
            });
    });
};