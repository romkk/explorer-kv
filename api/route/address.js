var mysql = require('../lib/mysql');
var Address = require('../lib/address');
var log = require('debug')('api:route:address');
var bitcore = require('bitcore');
var restify = require('restify');

module.exports = (server) => {
    server.get('/address/:addr', (req, res, next)=> {
        req.checkParams('addr', 'Not a valid address').isValidAddress();

        req.checkQuery('offset', 'should be a valid number').optional().isNumeric();
        req.sanitize('offset').defaultValue(0);
        req.sanitize('offset').toInt();

        req.checkQuery('limit', 'should be between 0 and 50').optional().isNumeric().isInt({ max: 50, min: 0});
        req.sanitize('limit').defaultValue(50);
        req.sanitize('limit').toInt();

        req.checkQuery('sort', 'should be desc or asc').optional().isIn(['desc', 'asc']);
        req.sanitize('limit').defaultValue('desc');

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
                return address.load(req.query.sort, req.query.offset, req.query.limit);
            })
            .then(address => {
                res.send(address);
                next();
            });
    });
};