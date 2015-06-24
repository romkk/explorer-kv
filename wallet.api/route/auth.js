var mysql = require('../lib/mysql');
var restify = require('restify');
var auth = require('../lib/auth');
var validate = require('../lib/valid_json');
var _ = require('lodash');
var bitcoind = require('../lib/bitcoind');
var moment = require('moment');

module.exports = server => {
    server.get('/auth/:wid', async (req, res, next) => {
        req.checkParams('wid', 'should be a valid wallet id').matches(/w_[a-f0-9]{64}/);

        var errors = req.validationErrors();
        if (errors) {
            return next(new restify.InvalidArgumentError({
                message: errors
            }));
        }

        var wid = req.params.wid;

        var challengeString = await auth.makeChallenge(wid);

        res.send(challengeString);
        next();
    });

    server.post('/auth/:wid', validate('verifyAuth'), async (req, res, next) => {
        var now = moment.utc().unix();
        var { challenge, signature, address } = req.body;

        var [challengeStr, selfSignature] = challenge.split('.');

        if (_.isUndefined(challengeStr) || _.isUndefined(selfSignature)) {
            res.send({
                success: false,
                code: 'AuthInvalidChallenge',
                message: 'invalid challenge string or signature'
            });
            return next();
        }

        challengeStr = auth.base64Decode(challengeStr);
        selfSignature = auth.base64Decode(selfSignature);

        if (challengeStr === false || selfSignature === false ||
                !auth.verifyHamc(challengeStr, selfSignature)) {
            res.send({
                success: false,
                code: 'AuthInvalidChallenge',
                message: 'invalid challenge string or signature, decode error'
            });
            return next();
        }

        var challengeInfo = JSON.parse(challengeStr);

        if (challengeInfo.address && challengeInfo.address != address) {
            res.send({
                success: false,
                code: 'AuthInvalidAddress',
                message: 'invalid address for the specified wid'
            });
            return next();
        }

        if (challengeInfo.expired_at < now) {
            res.send({
                success: false,
                code: 'AuthInvalidChallenge',
                message: 'invalid challenge string, timeout'
            });
            return next();
        }

        try {
            var valid = await bitcoind('verifymessage', address, signature, challenge);
            if (valid) {
                if (_.isUndefined(challengeInfo.address)) {        //新建绑定关系
                    let sql = `insert into wallet values (?, ?, now(), now())`;
                    try {
                        await mysql.query(sql, [challengeInfo.wid, address]);
                    } catch (err) {
                        if (err.code == 'ER_DUP_ENTRY') {   // 该绑定由其他请求已处理完成，需要根据 address 重新进行签名
                            res.send({
                                success: false,
                                code: 'AuthNeedBindAddress',
                                message: 'an address has been bind to the wid'
                            });
                            return next();
                        } else {
                            throw error;
                        }
                    }
                }

                // 生成新的 token
                let token = auth.issueToken(challengeInfo.wid, address);

                res.send(_.extend(token, {
                    success: true
                }));
            } else {
                res.send({
                    success: false,
                    code: 'AuthInvalidSignature',
                    message: 'invalid signature'
                });
            }
        } catch (err) {
            if (err.name == 'StatusCodeError') {
                res.send({
                    success: false,
                    code: 'AuthDenied',
                    message: err.error.error.message
                });
            } else {
                res.send(new restify.InternalServerError('RPC call error.'))
            }
        }

        next();
    });
};