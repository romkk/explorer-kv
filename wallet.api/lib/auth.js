var getIP = require('ipware')().get_ip;
var mysql = require('./mysql');
var moment = require('moment');
var restify = require('restify');
var _ = require('lodash');
var bitcore = require('bitcore');
var inet = require('inet');

module.exports = {
    findIP(req, res, next) {
        var info = getIP(req);
        var match = info.clientIp.match(/\b\d{1,3}(\.\d{1,3}){3}\b/g);
        req.ip = match ? match[0] : '0.0.0.0';
        next();
    },

    async challenge(ip, did, wid) {
        const maxUnfinishedChallenge = 3;
        // 检查是否登录次数过多
        var sql = `select count(id) as cnt from auth_challenge
                   where wid = ? and auth_finished = 0 and created_at >= ?`;
        var loginCount = await mysql.pluck(sql, 'cnt', [ wid, moment.utc().subtract(5, 'm').format('YYYY-MM-DD HH:mm:ss')]);
        if (loginCount >= maxUnfinishedChallenge) {
            return new restify.ForbiddenError('Too many auth requests.');
        }

        // 是否已有待验证字符串
        sql = `select challenge, expired_at from auth_challenge
               where wid = ? and auth_finished = 0 and device_id = ?
                 and request_ip = ?`;
        var row = await mysql.selectOne(sql, [wid, did, inet.aton(ip)]);
        if (row != null) {
            return row;
        }

        // 新建验证字符串
        var challengeString = bitcore.crypto.Random.getRandomBuffer(20).toString('base64');
        var expiredAt = moment.utc().add(5, 'm').unix();
        sql = `insert into auth_challenge
                    (wid, device_id, request_ip, request_ip_str, challenge, expired_at, auth_finished, created_at, updated_at)
               values
                    (?, ?, ?, ?, ?, ?, 0, now(), now())`;
        await mysql.query(sql, [wid, did, ip, inet.aton(ip), challengeString, expiredAt]);

        return {
            challenge: challengeString,
            expired_at: expiredAt
        };
    }
};