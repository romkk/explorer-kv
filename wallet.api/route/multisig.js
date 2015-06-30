let mysql = require('../lib/mysql');
let _ = require('lodash');
let MultiSig = require('../lib/multisig');
let validate = require('../lib/valid_json');
let PublicKey = require('bitcore').PublicKey;
let restify = require('restify');
let log = require('debug')('wallet:route:multisig');

module.exports = server => {
    server.post('/multi-signature-addr', validate('createMultiSignatureAccount'), async (req, res, next) => {
        let m = req.body.m, n = req.body.n,
            creatorName = req.body.creator_name,
            creatorPubkey = req.body.creator_pubkey;

        if (n > m) {
            res.send({
                success: false,
                code: 'MultiSignatureAccountInvalidParticipantCount',
                message: 'N can not be greater than m'
            });
            return next();
        }

        let validateResult = await MultiSig.pubkeyValid(creatorPubkey);
        if (validateResult !== true) {
            res.send({
                success: false,
                code: 'MultiSignatureAccountInvalidPubkey',
                message: 'Invalid pubkey found'
            });
            return next();
        }

        try {
            let result = await MultiSig.createAccount(req.token.wid, m, n, creatorPubkey, creatorName);
            res.send({
                success: true,
                multi_signature_id: result.multiAccountId
            });
        } catch (err) {
            res.send({
                success: false,
                code: 'MultiSignatureAccountCreateFailed',
                message: 'create failed'
            });
        }
        next();
    });

    //server.put('/multi-signature-addr/:id');

    server.get('/multi-signature-addr/:id', async (req, res, next) => {
        req.checkParams('id', 'Not a valid id').isInt();
        req.sanitize('id').toInt();

        let errors = req.validationErrors();
        if (errors) {
            return next(new restify.InvalidArgumentError({
                message: errors
            }));
        }

        let id = req.params.id;

        // get status
        try {
            let status = await MultiSig.getAccountStatus(req.token.wid, id);
            res.send(_.extend(status, {
                success: true
            }));
            next();
        } catch (err) {
            if (err instanceof restify.HttpError) {
                res.send(err);
            } else {
                log(`get multi-signature-addr by id error, code = ${err.code}, message = ${err.message}`);
                res.send(new restify.InternalServerError('Internal Error'));
            }
            next();
        }
    });
};