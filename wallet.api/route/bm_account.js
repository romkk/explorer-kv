let mysql = require('../lib/mysql');
let validate = require('../lib/valid_json');

module.exports = server => {
    server.post('/bm-account/bind', validate('bindBMAccount'), async (req, res, next) => {
        let accountId = req.body.bm_account;
        try {
            await mysql.transaction(async conn => {
                // 一个 BM 帐号只能对应一个 wid
                let sql = `select count(id) as cnt from bm_account where account_id = ? LOCK IN SHARE MODE`;
                let count = await conn.pluck(sql, 'cnt', [accountId]);
                if (count) {
                    let e = new Error();
                    e.code = 'BMAccountUsed';
                    e.message = 'this account has been used';
                    throw e;
                }

                sql = `insert into bm_account
                       (wid, account_id, created_at, updated_at)
                       values
                       (?, ?, now(), now())`;
                await conn.query(sql, [req.token.wid, accountId]);
            });
        } catch (err) {
            if (err.code && err.message) {
                res.send({
                    success: false,
                    code: err.code,
                    message: err.message
                });
            } else {
                res.send({
                    success: false,
                    code: 'BMAccountBindFailed',
                    message: 'please try again later'
                });
            }

            return next();
        }

        res.send({
            success: true,
            account: accountId,
            wids: [
                req.token.wid
            ]
        });
        return next();
    });
};