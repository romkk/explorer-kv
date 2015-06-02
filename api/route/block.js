var mysql = require('../lib/mysql');
var Block = require('../lib/block');
var log = require('debug')('api:route:block');
var restify = require('restify');

module.exports = (server) => {
    server.get('/rawblock/:blockIdentifier', (req, res, next) => {
        req.checkParams('fulltx', 'should be boolean').optional().isBoolean();
        req.sanitize('fulltx').toBoolean(true);

        req.checkQuery('offset', 'should be a valid number').optional().isNumeric();
        req.sanitize('offset').toInt();

        req.checkQuery('limit', 'should be between 10 and 50').optional().isNumeric().isInt({ max: 50, min: 10 });
        req.sanitize('limit').toInt();

        Block.make(req.params.blockIdentifier)
            .then(blk => {
                if (blk == null) {
                    return new restify.ResourceNotFoundError('Block not found');
                }
                return blk.load(req.params.offset, req.params.limit, req.params.fulltx);
            })
            .then(blk => {
                res.send(blk);
                next();
            });
    });
};