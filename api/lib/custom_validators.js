var bitcore = require('bitcore');

function isValidAddress(v) {
    return bitcore.Address.isValid(v);
}

module.exports = {
    isValidAddress: isValidAddress
};