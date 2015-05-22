var sprintf = require('sprintf').sprintf;
var mysql = require('./mysql');
var log = require('debug')('api:lib:helper');



module.exports ={
    constant: {
        ID_IDENTIFIER: Symbol('id'),
        HASH_IDENTIFIER: Symbol('hash')
    },
    paramType(str) {
        str = String(str);
        return parseInt(str, 10).toString() === str ? this.constant.ID_IDENTIFIER : this.constant.HASH_IDENTIFIER;
    }
};