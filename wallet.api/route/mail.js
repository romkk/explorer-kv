let restify = require('restify');
let mail = require('../lib/mail');
let fs = require('fs');
let _ = require('lodash');
let log = require('debug')('wallet:route:mail');

module.exports = server => {
    server.post('/mail', async (req, res, next) => {
        req.checkParams('receiver', 'should be a valid email').isEmail();
        req.sanitize('txhash').toString();

        var errors = req.validationErrors();

        if (errors) {
            return next(new restify.InvalidArgumentError({
                message: errors
            }));
        }

        let receiver = req.params.receiver;
        let f = _.get(req, 'files.file', null); //可能没有上传文件


        if (!f) {
            res.send(new restify.BadRequestError('BadRequestError'));
            return next();
        }

        log(`发送备份文件邮件 receiver = ${receiver}`);

        let ret;
        try {
            ret = await mail(receiver, f.path, f.name);
        } catch (err) {
            res.send(new restify.InternalServerError('Internal Error'));
            return next();
        }

        log(`发送成功 receiver = ${receiver}, msg_id = ${ret.msg_key}`);

        res.send(_.extend({ success: true }, ret));

        next();
    });
};