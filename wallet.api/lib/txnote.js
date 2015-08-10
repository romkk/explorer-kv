let _ = require('lodash');
let mysql = require('./mysql');
let bitcoind = require('./bitcoind');

module.exports = {
    async getNote(txhash) {
        let sql = `select note
                   from tx_note
                   where txhash = ?`;
        return (await mysql.pluck(sql, 'note', [txhash]));
    },
    async setNote(txhash, note) {
        // 验证 hash 是否正确
        try {
            await bitcoind('getrawtransaction', txhash);
        } catch (err) {
            if (err.name = 'StatusCodeError' && _.get(err, 'error.error.code') == -8) {
                let e = new Error();
                e.code = 'TxNoteInvalidHash';
                e.message = 'invalid txhash';
                throw e;
            } else {
                throw err;
            }
        }
        // 保存
        let sql = `insert into tx_note
                   (txhash, note, created_at, updated_at)
                   values
                   (?, ?, now(), now())`;
        try {
            await mysql.query(sql, [txhash, note]);
        } catch (err) {
            if (err.code == 'ER_DUP_ENTRY') {
                let e = new Error();
                e.code = 'TxNoteCreated';
                e.message = 'tx note has been created';
                throw e;
            } else {
                throw err;
            }
        }
    }
};