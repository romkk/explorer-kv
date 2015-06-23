var mysql = require('../lib/mysql');
var _ = require('lodash');
var restify = require('restify');
var log = require('debug')('wallet:route:device');

module.exports = server => {
    server.post('/device/:wid/:did', async (req, res, next) => {
        req.checkParams('did', 'should be a valid device_id').isHexadecimal();
        req.checkParams('wid', 'should be a valid wallet_id').matches(/w_[a-f0-9]{64}/);

        var did = req.params.did,
            wid = req.params.wid;

        var errors = req.validationErrors();
        if (errors) {
            return next(new restify.InvalidArgumentError({
                message: errors
            }));
        }

        var sql, count;
        sql = `select * from device where wid = ? and did = ? limit 1`;
        var row = await mysql.selectOne(sql, [wid, did]);
        if (!_.isNull(row)) {
            res.send({ success: true });
            return next();
        }

        // 是否超出数量限制
        sql = `select count(id) as cnt from device where wid = ? limit 1`;
        count = await mysql.pluck(sql, 'cnt', [wid]);
        if (count >= 16) {
            res.send({
                success: false,
                code: 'RegisterTooManyDevices',
                message: 'wid has been registered with too many devices, please delete some and try again.'
            });
            return next();
        }

        // 是否超出 wid 与 did 的一对多限制
        sql = `select count(id) as cnt from device where did = ?`;
        count = await mysql.pluck(sql, 'cnt', [did]);
        if (count) {
            res.send({
                success: false,
                code: 'RegisterDeviceAlreadyTaken',
                message: 'the device has been taken'
            });
            return next();
        }

        // 插入新的记录
        sql = `insert into device (wid, did, created_at, updated_at) values ( ?, ?, now(), now() )`;
        await mysql.query(sql, [wid, did]);
        res.send({ success: true});
        next();
    });

    server.del('/device/:wid/:did', async (req, res, next) => {
        req.checkParams('did', 'should be a valid device_id').isHexadecimal();
        req.checkParams('wid', 'should be a valid wallet_id').matches(/w_[a-f0-9]{64}/);

        var did = req.params.did,
            wid = req.params.wid;

        var errors = req.validationErrors();
        if (errors) {
            return next(new restify.InvalidArgumentError({
                message: errors
            }));
        }

        var sql = `delete from device where did = ? and wid = ?`;
        await mysql.query(sql, [did, wid]);

        res.send({ success: true });
        next();
    });
};