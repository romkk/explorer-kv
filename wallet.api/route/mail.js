let restify = require('restify');
let mail = require('../lib/mail');
let fs = require('fs');
let _ = require('lodash');

module.exports = server => {
    server.post('/sendmail', async (req, res, next) => {
        res.send(_.extend(req.params, req.files));
        next();
    });

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
        let f = req.files.file;

        if (!f) {
            res.send(new restify.BadRequestError('BadRequestError'));
            return next();
        }

        if (f.type != 'application/zip') {
            res.send({
                success: false,
                code: 'MailInvalidHeader',
                message: 'the file must be a valid zip file'
            });
            return next();
        }

        let ret;
        try {
            ret = await mail(receiver, f.path, f.name);
        } catch (err) {
            console.log(err);
            res.send(new restify.InternalServerError('Internal Error'));
            return next();
        }

        console.log(ret);

        res.send(_.extend({ success: true }, ret));

        next();
    });
};