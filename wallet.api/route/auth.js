var mysql = require('mysql');
var restify = require('restify');
var auth = require('../lib/auth');

module.exports = server => {
    server.get('/auth', async (req, res, next) => {
        req.checkQuery('device_id', 'should be a valid number').isAlphanumeric();
        req.checkQuery('wid', 'should be a valid wallet id').matches(/w_[a-f0-9]{64}/);

        var errors = req.validationErrors();
        if (errors) {
            return next(new restify.InvalidArgumentError({
                message: errors
            }));
        }

        var did = req.params.device_id;
        var wid = req.params.wid;
        var ip = req.ip;

        var challengeString = await auth.challenge(ip, did, wid);

        res.send(challengeString);
        next();
    });

    server.post('/auth', async (req, res, next) => {

    });

    server.post('/auth/verify', async (req, res, next) => {

    });
};