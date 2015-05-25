var sprintf = require('sprintf').sprintf;
var mysql = require('./mysql');
var bs58 = require('bs58');
var log = require('debug')('api:lib:helper');

module.exports ={
    constant: {
        ID_IDENTIFIER: Symbol('id'),
        HASH_IDENTIFIER: Symbol('hash')
    },
    paramType(str) {
        str = String(str);
        return parseInt(str, 10).toString() === str ? this.constant.ID_IDENTIFIER : this.constant.HASH_IDENTIFIER;
    },
    addressToHash160(addr) {
        addr = bs58.decode(addr);
        addr = new Buffer(addr).toString('hex');
        return addr.slice(2, addr.length - 8);
    }
};