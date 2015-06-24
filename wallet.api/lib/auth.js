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
var config = require('config');

if (_.isUndefined(process.env.SECRET_KEY)) {
    log('[WARN] 没有指定 secret key');
}

var secretKey = new Buffer(process.env.SECRET_KEY || 'my mysterious key', 'utf8');

module.exports = {
    findIP(req, res, next) {
        var info = getIP(req);
        var match = info.clientIp.match(/\b\d{1,3}(\.\d{1,3}){3}\b/g);
        req.ip = match ? match[0] : '0.0.0.0';
        next();
    },

    issueToken(wid, address) {
        var now = moment.utc().unix();
        var token = {
            expired_at: now + config.get('tokenExpiredOffset'),
            wid: wid,
            address: address
        };

        return {
            token: module.exports.base64Encode(JSON.stringify(token)),
            expired_at: token.expired_at
        };
    },

    verifyToken(tokenEncoded) {
        var now = moment.utc().unix();
        var decoded = module.exports.base64Decode(tokenEncoded);
        if (decoded === false) {
            return false;
        }
        var token;
        try {
            token = JSON.parse(decoded);
        } catch (err) {
            return false;
        }

        if (token.expired_at <= now) {
            return false;
        }

        return token;
    },

    verifyHamc(str, signature) {
        return Hash.sha256hmac(new Buffer(str, 'utf8'), secretKey).toString('utf8') === signature;
    },

    async makeChallenge(wid) {
        var now = moment.utc().unix();
        var challenge = {
            wid: wid,
            expired_at: now + 300,
            nonce: Random.getRandomBuffer(5).toString('hex')
        };

        // 检查是否已注册过
        var sql = `select address_bind from wallet where wid = ? limit 1`;
        var addressBind = await mysql.pluck(sql, 'address_bind', [wid]);
        if (!_.isNull(addressBind)) {
            challenge.address = addressBind;
        }

        challenge = JSON.stringify(challenge);

        var hmac = Hash.sha256hmac(new Buffer(challenge, 'utf8'), secretKey);

        var ret = {
            challenge: module.exports.base64Encode(challenge) + '.' + module.exports.base64Encode(hmac),
            expired_at: now + 300
        };

        if (!_.isNull(addressBind)) {
            ret.address = addressBind;
        }

        return ret;
    },

    base64Encode(str) {
        return _.trimRight(new Buffer(str, 'utf8').toString('base64'), '=');
    },

    base64Decode(str) {
        try {
            return new Buffer(str, 'base64').toString('utf8');
        } catch (err) {
            return false;
        }
    }
};