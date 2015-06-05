var bitcore = require('bitcore');
var _ = require('lodash');
var restify = require('restify');

function isValidAddress(v) {
    return bitcore.Address.isValid(v);
}

function isValidAddressList(active) {
    if (active == null || active.length === 0) {
        return new restify.MissingParameterError('Param `active` not found');
    }

    var parts = active.trim().split('|');

    if (parts.length > 256) {
        return new restify.InvalidArgumentError('Too many addresses');
    }

    if (_.uniq(parts).length != parts.length) {
        return new restify.InvalidArgumentError('Duplicate addresses found');
    }

    if (_.compact(parts).length != parts.length) {
        return new restify.InvalidArgumentError('Invalid address found');
    }

    for (let p of parts) {
        if (!module.exports.isValidAddress(p)) {
            return new restify.InvalidArgumentError(`Invalid address found: ${p}`);
        }
    }

    return null;
}

module.exports = {
    isValidAddress: isValidAddress,
    isValidAddressList: isValidAddressList
};