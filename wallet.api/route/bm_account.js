let mysql = require('../lib/mysql');
let validate = require('../lib/valid_json');
let uc = require('../lib/usercenter');
let restify = require('restify');

module.exports = server => {
    server.post('/bm-account/bind', validate('bindBMAccount'), async (req, res, next) => {
        let ticket = req.body.ticket;

        try {
            let accountId;

            await mysql.transaction(async conn => {
                // 验证 ticket 是否有效
                let userInfo = await uc.get(ticket);
                if (!userInfo) {
                    let e = new Error();
                    e.code = 'BMAccountInvalidTicket';
                    e.message = 'invalid ticket';
                    throw e;
                }

                accountId = Number(userInfo.data.id);

                // 一个 BM 帐号只能对应一个 wid
                let sql = `select wid, account_id from bm_account where account_id = ? for update`;
                let row = await conn.selectOne(sql, [accountId]);
                if (row) {
                    if (row.account_id == accountId && row.wid == req.token.wid) return;

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

            res.send({
                success: true,
                account: accountId,
                wids: [
                    req.token.wid
                ]
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
        }

        next();
    });

    server.get('/bm-account', async (req, res, next) => {
        req.checkParams('ticket', 'can not be empty').isLength(1);
        req.sanitize('ticket').toString();

        let errors = req.validationErrors();
        if (errors) {
            return next(new restify.InvalidArgumentError({
                message: errors
            }));
        }

        let ticket = req.params.ticket;

        let userInfo = await uc.get(ticket);
        if (!userInfo) {
            res.send({
                success: false,
                code: 'BMAccountInvalidTicket',
                message: 'invalid ticket'
            });
            return next();
        }

        let accountId = +userInfo.data.id;
        let sql = `select * from bm_account where account_id = ?`;
        let rows = await mysql.query(sql, [accountId]);
        res.send({
            success: true,
            account: accountId,
            wids: rows.map(r => r.wid)
        });
        return next();
    });
};