var mysql = require('../lib/mysql');
var Address = require('../lib/address');
var log = require('debug')('api:route:address');
var bitcore = require('bitcore');
var helper = require('../lib/helper');
var restify = require('restify');
var _ = require('lodash');
var moment = require('moment');
var validators = require('../lib/custom_validators');

module.exports = (server) => {
    server.get('/address/:addr', (req, res, next)=> {
        req.checkParams('addr', 'Not a valid address').isValidAddress();

        req.checkQuery('offset', 'should be a valid number').optional().isNumeric();
        req.sanitize('offset').toInt();

        req.checkQuery('timestamp', 'should be a valid timestamp').optional().isNumeric({ min: 0, max: moment.utc().unix() + 3600 });    // +3600 以消除误差
        req.sanitize('timestamp').toInt();

        req.checkQuery('limit', 'should be between 1 and 50').optional().isNumeric().isInt({ max: 50, min: 1});
        req.sanitize('limit').toInt();

        req.checkQuery('sort', 'should be desc or asc').optional().isIn(['desc', 'asc']);

        var errors = req.validationErrors();

        if (errors) {
            return next(new restify.InvalidArgumentError({
                message: errors
            }));
        }

        Address.make(req.params.addr)
            .then(address => {
                if (address == null) {
                    return new restify.ResourceNotFoundError('Address not found');
                }
                return address.load(
                    req.params.timestamp,
                    req.params.sort,
                    req.params.offset,
                    req.params.limit
                );
            })
            .then(address => {
                res.send(address);
                next();
            });
    });

    server.get('/multiaddr', (req, res, next) => {
        var err = validators.isValidAddressList(req.query.active);
        if (err != null) {
            return next(err);
        }

        var parts = req.params.active.trim().split('|');

        Promise.all(parts.map(p => {
            return Address.make(p)
                .then(addr => {
                    if (addr == null) {
                        throw new restify.InvalidArgumentError(`Address not found: ${p}`);
                    }
                    var a = addr.attrs;
                    return {
                        final_balance: a.total_received - a.total_sent,
                        address: a.address,
                        total_sent: a.total_sent,
                        total_received: a.total_received,
                        n_tx: a.tx_count,
                        hash160: helper.addressToHash160(a.address)
                    };
                });
        })).then((addrs) => {
            res.send({
                addresses: addrs
            });
            next();
        }, err => {
            next(err);
        })
    });
};