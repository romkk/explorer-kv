var mysql = require('../lib/mysql');
var Block = require('../lib/block');
var log = require('debug')('api:route:block');
var restify = require('restify');
var sb = require('../lib/ssdb')();
var _ = require('lodash');
var moment = require('moment');

module.exports = (server) => {
    server.get('/rawblock/:blockIdentifier', async (req, res, next) => {
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

        let blk;
        try {
            blk = await Block.grab(req.params.blockIdentifier, req.params.offset, req.params.limit, req.params.fulltx, !req.params.skipcache);
        } catch (err) {
            res.send(new restify.ResourceNotFoundError('Block not found'));
            console.log(err);
        }
        
        let nextBlock = await Block.getNextBlock(blk.height, blk.chain_id, !req.params.skipcache);
        blk.next_block = _.get(nextBlock, 'hash', null);
        res.send(blk);
        next();
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

    server.get('/block', async (req, res, next) => {
        req.checkQuery('timestamp', 'should be a valid timestamp').optional().isNumeric({ min: 0, max: moment.utc().unix() + 3600 });    // +3600 以消除误差
        req.sanitize('timestamp').toInt();

        req.checkQuery('offset', 'should be a valid number').optional().isNumeric();
        req.sanitize('offset').toInt();

        req.checkQuery('limit', 'should be between 1 and 50').optional().isNumeric().isInt({ max: 50, min: 1});
        req.sanitize('limit').toInt();

        req.checkQuery('sort', 'should be desc or asc').optional().isIn(['desc', 'asc']);

        let errors = req.validationErrors();

        if (errors) {
            return next(new restify.InvalidArgumentError({
                message: errors
            }));
        }

        let offset = _.get(req, 'params.offset', 0);
        let limit = _.get(req, 'params.limit', 50);
        let sort = _.get(req, 'params.sort', 'desc');
        let timestamp = _.get(req, 'params.timestamp', sort == 'desc' ? moment.utc().unix() + 3600 : 0);

        res.send(await Block.getBlockList(timestamp, offset, limit, sort, !req.params.skipcache));
        next();
    });
};