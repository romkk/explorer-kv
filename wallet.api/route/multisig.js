let mysql = require('../lib/mysql');
let _ = require('lodash');
let MultiSig = require('../lib/multisig');
let validate = require('../lib/valid_json');
let PublicKey = require('bitcore').PublicKey;
let restify = require('restify');
let log = require('debug')('wallet:route:multisig');
let bitcoind = require('../lib/bitcoind');
let assert = require('assert');

module.exports = server => {
    server.post('/multi-signature-addr', validate('createMultiSignatureAccount'), async (req, res, next) => {
        let m = req.body.m, n = req.body.n,
            creatorName = req.body.creator_name,
            creatorPubkey = req.body.creator_pubkey,
            accountName = req.body.account_name;

        if (n > m) {
            res.send({
                success: false,
                code: 'MultiSignatureAccountInvalidParticipantCount',
                message: 'N can not be greater than m'
            });
            return next();
        }

        let validateResult = await MultiSig.pubkeyValid(creatorPubkey);
        if (!validateResult) {
            res.send({
                success: false,
                code: 'MultiSignatureAccountInvalidPubkey',
                message: 'Invalid pubkey found'
            });
            return next();
        }

        try {
            let result = await MultiSig.createAccount(req.token.wid, accountName, m, n, creatorPubkey, creatorName);
            res.send(_.extend({}, await MultiSig.getAccountStatus(req.token.wid, result.multiAccountId), {
                success: true
            }));
        } catch (err) {
            res.send({
                success: false,
                code: 'MultiSignatureAccountCreateFailed',
                message: 'create failed'
            });
        }
        next();
    });

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

    server.put('/multi-signature-addr/:id', validate('updateMultiSignatureAccount'), async (req, res, next) => {
        req.checkParams('id', 'Not a valid id').isInt();
        req.sanitize('id').toInt();

        let errors = req.validationErrors();
        if (errors) {
            return next(new restify.InvalidArgumentError({
                message: errors
            }));
        }

        let participantName = req.body.name,
            participantPubkey = req.body.pubkey,
            id = req.params.id;

        if (!(await MultiSig.pubkeyValid(participantPubkey))) {
            res.send({
                success: false,
                code: 'MultiSignatureAccountInvalidPubkey',
                message: 'Invalid pubkey found'
            });
            return next();
        }

        let status;
        try {
            status = await MultiSig.getAccountStatus(req.token.wid, id);
        } catch (err) {
            if (err instanceof restify.HttpError) {
                res.send(err);
            } else {
                log(`get multi-signature-addr by id error, code = ${err.code}, message = ${err.message}`);
                res.send(new restify.InternalServerError('Internal Error'));
            }
            return next();
        }

        if (!(await MultiSig.participantNameValid(status.id, participantName))) {
            res.send({
                success: false,
                code: 'MultiSignatureAccountDuplicateName',
                message: 'duplicate name found'
            });
            return next();
        }

        if (status.complete || status.participants.length >= status.m) {
            res.send({
                success: false,
                code: 'MultiSignatureAccountJoinCompleted',
                message: 'you are too late'
            });
            return next();
        }

        let newPos = _.last(status.participants).pos + 1;
        let insertId;
        try {
            insertId = await MultiSig.joinAccount(id, req.token.wid, participantName, participantPubkey, newPos, status);
        } catch (err) {
            if (err.code == 'ER_DUP_ENTRY') {       // 竞态条件冲突
                res.send({
                    success: false,
                    code: 'MultiSignatureAccountJoinFailed',
                    message: 'please try again later'
                });
                return next();
            } else {
                throw err;
            }
        }

        // completed?
        status = await MultiSig.getAccountStatus(req.token.wid, id);
        if (status.participants.length < status.m) {
            res.send(_.extend({}, status, { success: true }));
            return next();
        }

        // generate new address
        let result;
        try {
            result = await bitcoind('createmultisig', status.n, _.pluck(status.participants, 'pubkey'));
        } catch (err) {     //bitcoind error
            if (err.name == 'StatusCodeError') {
                res.send({
                    success: false,
                    bitcoind: {
                        statusCode: err.statusCode,
                        body: err.response.body
                    },
                    message: 'create multisig failed',
                    code: 'MultiSignatureAccountJoinFailed'
                });
            } else {
                res.send(new restify.InternalServerError('RPC call error.'));
            }

            let sql = `delete from multisig_account_participant where id = ?`;
            mysql.query(sql, [insertId]);

            return next();
        }

        let sql = `update multisig_account set redeem_script = ?, generated_address = ?, updated_at = now()
                   where id = ?`;
        let updateResult = await mysql.query(sql, [result.redeemScript, result.address, id]);
        assert(updateResult.affectedRows == 1);

        res.send(_.extend({}, await MultiSig.getAccountStatus(req.token.wid, id), { success: true }));
        next();
    });
};