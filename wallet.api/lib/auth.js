var getIP = require('ipware')().get_ip;
var mysql = require('./mysql');
var moment = require('moment');
var restify = require('restify');
var _ = require('lodash');
var bitcore = require('bitcore');
var inet = require('inet');
var Hash = require('bitcore').crypto.Hash;
var Random = require('bitcore').crypto.Random;
var log = require('debug')('wallet:lib:auth');
var bitcoind = require('./bitcoind');
var Address = require('bitcore').Address;

if (_.isUndefined(process.env.SECRET_KEY)) {
    log('[WARN] 没有指定 secret key');
}

var secretKey = new Buffer(process.env.SECRET_KEY || 'my mysterious key', 'utf8');

function base64Encode(str) {
    return _.trimRight(new Buffer(str, 'utf8').toString('base64'), '=');
}

function base64Decode(str) {
    return new Buffer(str, 'base64').toString('utf8');
}

module.exports = {
    findIP(req, res, next) {
        var info = getIP(req);
        var match = info.clientIp.match(/\b\d{1,3}(\.\d{1,3}){3}\b/g);
        req.ip = match ? match[0] : '0.0.0.0';
        next();
    },

    issueToken(wid, address) {

    },

    verifyToken() {

    },

    makeChallenge(did, wid) {
        var now = moment.utc().unix();
        var challenge = {
            did: did,
            wid: wid,
            expired_at: now + 300,
            nonce: Random.getRandomBuffer(3).toString('hex')
        };

        challenge = JSON.stringify(challenge);

        var hmac = Hash.sha256hmac(new Buffer(challenge, 'utf8'), secretKey);

        return {
            challenge: base64Encode(challenge) + '.' + base64Encode(hmac),
            expired_at: now + 300
        };
    },

    decodeChallengeString(challengeStr) {
        if (_.isUndefined(challengeStr)) {
            return false;
        }

        try {
            console.log(JSON.parse(base64Decode(challengeStr)));
            return JSON.parse(base64Decode(challengeStr));
        } catch (err) {
            return false;
        }
    },

    async verifyChallenge(signature, address, challengeString) {
        var challenge = module.exports.decodeChallengeString(challengeString);
        if (challenge === false) {
            let e = new Error('invalid challenge string, parse error');
            e.code = 'AuthInvalidChallenge';
            return e;
        }
        var now = moment.utc().unix();
        if (challenge.expired_at <= now) {
            let e = new Error('invalid challenge string, timeout');
            e.code = 'AuthInvalidChallenge';
            return e;
        }

        try {
            var valid = await bitcoind('verifymessage', address, signature, challengeString);
            if (valid) {
                return {
                    token: 'token',
                    expired_at: '12345'
                };
            } else {
                let e = new Error('invalid signature');
                e.code = 'AuthInvalidSignature';
                return e;
            }
        } catch (err) {
            log(`与 bitcoind 通信失败，error = ${JSON.stringify(err.error)}`);
            throw err;
        }
    }
};