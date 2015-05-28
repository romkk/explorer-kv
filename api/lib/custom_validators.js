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

    if (parts.length > 128) {
        return new restify.InvalidArgumentError('Too many addresses');
    }

    if (_.uniq(parts).length != parts.length) {
        return new restify.InvalidArgumentError('Duplicate addresses found');
    }

    if (!parts.every(v => module.exports.isValidAddress(v))) {
        return new restify.InvalidArgumentError('Invalid address found');
    }

    return null;
}

module.exports = {
    isValidAddress: isValidAddress,
    isValidAddressList: isValidAddressList
};