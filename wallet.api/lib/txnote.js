let _ = require('lodash');
let mysql = require('./mysql');
let bitcoind = require('./bitcoind');

module.exports = {
    async getNote(wid, txhash) {
        let sql = `select note
                   from tx_note
                   where wid = ? and txhash = ?`;
        return (await mysql.pluck(sql, 'note', [wid, txhash]));
    }
};