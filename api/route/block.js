var mysql = require('../lib/mysql');
var Block = require('../lib/block');
var log = require('debug')('api:route:block');
var restify = require('restify');

module.exports = (server) => {
    server.get('/rawblock/:blockIdentifier', (req, res, next) => {
        req.checkQuery('fulltx', 'should be boolean').optional().isBoolean();
        req.sanitize('fulltx').toBoolean(true);

        req.checkQuery('offset', 'should be a valid number').optional().isNumeric();
        req.sanitize('offset').toInt();

        req.checkQuery('limit', 'should be between 1 and 50').optional().isNumeric().isInt({ max: 50, min: 1 });
        req.sanitize('limit').toInt();

        Block.grab(req.params.blockIdentifier, req.params.offset, req.params.limit, req.params.fulltx, !req.params.skipcache)
            .then(blk => {
                res.send(blk);
                next();
            }, () => {
                res.send(new restify.ResourceNotFoundError('Block not found'));
                next();
            });
    });

    server.get('/block-height/:height', (req, res, next) => {
        req.checkParams('height', 'invalid height').optional().isInt({ min: 0 });
        req.sanitize('height').toInt();

        var errors = req.validationErrors();

        if (errors) {
            return next(new restify.InvalidArgumentError({
                message: errors
            }));
        }

        var sql = `select block_id
                   from 0_blocks
                   where height = ?
                   order by chain_id asc`;

        mysql.list(sql, 'block_id',[ req.params.height ])
            .then(ids => Promise.map(ids, id => {
                return Block.make(id).then(blk => blk.load(0, 0, false));
            }))
            .then(blk => {
                res.send(blk);
                next();
            });
    });
};