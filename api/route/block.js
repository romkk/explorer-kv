var mysql = require('../lib/mysql');
var Block = require('../lib/block');
var log = require('debug')('api:route:block');
var restify = require('restify');
var sb = require('../lib/ssdb')();

module.exports = (server) => {
    server.get('/rawblock/:blockIdentifier', (req, res, next) => {
        req.checkQuery('fulltx', 'should be boolean').optional().isBoolean();
        req.sanitize('fulltx').toBoolean(true);

        req.checkQuery('offset', 'should be a valid number').optional().isNumeric();
        req.sanitize('offset').toInt();

        req.checkQuery('limit', 'should be between 1 and 50').optional().isInt({ max: 50, min: 1 });
        req.sanitize('limit').toInt();

        var errors = req.validationErrors();

        if (errors) {
            return next(new restify.InvalidArgumentError({
                message: errors
            }));
        }

        Block.grab(req.params.blockIdentifier, req.params.offset, req.params.limit, req.params.fulltx, !req.params.skipcache)
            .then(blk => {
                res.send(blk);
                next();
            }, () => {
                res.send(new restify.ResourceNotFoundError('Block not found'));
                next();
            });
    });

    server.get('/block-height/:height', async (req, res, next) => {
        req.checkParams('height', 'invalid height').optional().isInt({ min: 0 });
        req.sanitize('height').toInt();

        var errors = req.validationErrors();

        if (errors) {
            return next(new restify.InvalidArgumentError({
                message: errors
            }));
        }

        res.send(await Block.grabByHeight(req.params.height, !req.params.skipcache));
        next();
    });
};