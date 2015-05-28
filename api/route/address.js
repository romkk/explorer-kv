var mysql = require('../lib/mysql');
var Address = require('../lib/address');
var log = require('debug')('api:route:address');
var bitcore = require('bitcore');
var helper = require('../lib/helper');
var restify = require('restify');
var _ = require('lodash');
var validators = require('../lib/custom_validators');

module.exports = (server) => {
    server.get('/address/:addr', (req, res, next)=> {
        req.checkParams('addr', 'Not a valid address').isValidAddress();

        req.checkQuery('offset', 'should be a valid number').optional().isNumeric();
        req.sanitize('offset').toInt();

        req.checkQuery('limit', 'should be between 10 and 50').optional().isNumeric().isInt({ max: 50, min: 1});
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
        var active = req.query.active;
        if (active == null || active.length === 0) {
            return next(new restify.MissingParameterError('Param `active` not found'));
        }

        var parts = active.trim().split('|');

        if (parts.length > 128) {
            return next(new restify.InvalidArgumentError('Too many addresses'));
        }

        if (_.uniq(parts).length != parts.length) {
            return next(new restify.InvalidArgumentError('Duplicate addresses found'));
        }

        if (!parts.every(v => validators.isValidAddress(v))) {
            return next(new restify.InvalidArgumentError('Invalid address found'));
        }

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