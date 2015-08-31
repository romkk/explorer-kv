let STSToken = require('../lib/sts');
let restify = require('restify');
let mysql = require('../lib/mysql');
let _ = require('lodash');
let auth = require('../lib/auth');
let uc = require('../lib/usercenter');
let config = require('config');
let log = require('debug')('wallet:route:sts');

module.exports = server => {
    server.get('/sts-token', async (req, res, next) => {
        let name, wid;

        //根据 token 或者 ticket 区分权限
        let token = req.headers['x-wallet-token'];
        let ticket = req.params.ticket;

        if (token) {
            log(`用户使用 token(${token}) 获取 sts-token`);
            token = auth.verifyToken(token);
            if (token) {
                wid = token.wid;
                let sql = `select id from wallet where wid = ?`;
                name = await mysql.pluck(sql, 'id',[wid]);
            }
        } else if (ticket) {
            log(`用户使用 ticket(${ticket}) 获取 sts-token`);
            let userInfo = await uc.get(ticket);
            if (userInfo) {
                name = userInfo.data.username;
                let sql = `select wid from bm_account where account_id = ?`;
                wid = await mysql.pluck(sql, 'wid', [ +userInfo.data.id ]); //如果没有记录则为 null
            }
        }

        if (name && wid) {
            log(`name = ${name}, wid = ${wid}`);
            let result;
            try {
                result = await (STSToken.make(name, wid));
            } catch (err) {
                console.log(err.stack);
                res.send(new restify.InternalServerError('Internal Error'));
                return next();
            }

            res.send(_.pick(result, ['Credentials', 'FederatedUser']));
        } else {
            res.send(new restify.UnauthorizedError('invalid token or ticket'));
        }
        return next();
    });
};