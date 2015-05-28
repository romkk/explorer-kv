var bitcore = require('bitcore');

module.exports = {
    isValidAddress: v => bitcore.Address.isValid(v)
};