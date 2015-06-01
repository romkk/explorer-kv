var sprintf = require('sprintf').sprintf;
var mysql = require('./mysql');
var bs58 = require('bs58');
var BN = require('bn.js');
var log = require('debug')('api:lib:helper');

module.exports ={
    constant: {
        ID_IDENTIFIER: Symbol('id'),
        HASH_IDENTIFIER: Symbol('hash')
    },
    paramType(str) {
        str = String(str);
        return new BN(str, 10).toString(10) === str ? this.constant.ID_IDENTIFIER : this.constant.HASH_IDENTIFIER;
    },
    addressToHash160(addr) {
        addr = bs58.decode(addr);
        addr = new Buffer(addr).toString('hex');
        return addr.slice(2, addr.length - 8);
    },
    toBigEndian(str) {
        var ret = [];
        String(str).replace(/\w{2}/g, match => ret.push(match));
        return ret.reverse().join('');
    }
};