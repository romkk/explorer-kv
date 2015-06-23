var mysql = require('mysql');
var restify = require('restify');
var auth = require('../lib/auth');
var validate = require('../lib/valid_json');
var _ = require('lodash');

module.exports = server => {
    server.get('/auth/:wid/:did', (req, res, next) => {
        req.checkParams('did', 'should be a valid device id').isAlphanumeric();
        req.checkParams('wid', 'should be a valid wallet id').matches(/w_[a-f0-9]{64}/);

        var errors = req.validationErrors();
        if (errors) {
            return next(new restify.InvalidArgumentError({
                message: errors
            }));
        }

        var wid = req.params.wid;
        var did = req.params.did;

        var challengeString = auth.makeChallenge(did, wid);

        res.send(challengeString);
        next();
    });

    server.post('/auth/verify', validate('verifyAuth'), async (req, res, next) => {
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

        try {
            var result = await auth.verifyChallenge(address, signature, challengeStr);
            if (result instanceof Error) {
                res.send({
                    success: false,
                    code: result.code,
                    message: result.message
                });
            } else {
                res.send({
                    success: true,
                    token: result.token,
                    expired_at: result.expired_at
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