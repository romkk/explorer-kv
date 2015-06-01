var mysql = require('../lib/mysql');
var Block = require('../lib/block');
var log = require('debug')('api:route:block');
var restify = require('restify');

module.exports = (server) => {
    server.get('/rawblock/:blockIdentifier', (req, res, next) => {
        req.checkParams('fulltx', 'should be boolean').optional().isBoolean();
        req.sanitize('fulltx').toBoolean(true);

        Block.make(req.params.blockIdentifier)
            .then(blk => {
                if (blk == null) {
                    return new restify.ResourceNotFoundError('Block not found');
                }
                return blk.load(req.params.fulltx);
            })
            .then(blk => {
                res.send(blk);
                next();
            });
    });
};