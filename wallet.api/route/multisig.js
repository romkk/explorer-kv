let mysql = require('../lib/mysql');
let _ = require('lodash');
let MultiSig = require('../lib/multisig');
let validate = require('../lib/valid_json');
let PublicKey = require('bitcore').PublicKey;
let restify = require('restify');
let log = require('debug')('wallet:route:multisig');
let bitcoind = require('../lib/bitcoind');
let assert = require('assert');
let bitcore = require('bitcore');

module.exports = server => {
    server.post('/multi-signature-account', validate('createMultiSignatureAccount'), async (req, res, next) => {
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

    server.get('/multi-signature-account/:id', async (req, res, next) => {
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

    server.put('/multi-signature-account/:id', validate('updateMultiSignatureAccount'), async (req, res, next) => {
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

        if (status.participants.length < status.m) {
            // TODO: push message to master and other slaves
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
                   where id = ? and is_deleted = 0`;
        let updateResult = await mysql.query(sql, [result.redeemScript, result.address, id]);

        if (updateResult.affectedRows == 0) {   // 更新失败，可能该账户已被取消创建
            res.send(new restify.NotFoundError('MultiSignatureAccount not found'));
            return next();
        }

        // TODO: push message to master and other slaves
        res.send(_.extend({}, await MultiSig.getAccountStatus(req.token.wid, id), { success: true }));
        next();
    });

    server.del('/multi-signature-account/:id', async (req, res, next) => {
        req.checkParams('id', 'Not a valid id').isInt();
        req.sanitize('id').toInt();

        let errors = req.validationErrors();
        if (errors) {
            return next(new restify.InvalidArgumentError({
                message: errors
            }));
        }

        let sql = `select v1.id, v1.is_deleted, v1.generated_address
                   from multisig_account v1
                     join multisig_account_participant v2
                       on v1.id = v2.multisig_account_id
                   where v1.id = ? and v2.wid = ? and v2.role = 'Initiator'
                   limit 1`;

        let row = await mysql.selectOne(sql, [req.params.id, req.token.wid]);

        if (_.isNull(row) || !!row.is_deleted) {
            res.send(new restify.NotFoundError('MultiSignatureAccount not found'));
            return next();
        } else if (!_.isNull(row.generated_address)) {
            res.send({
                success: false,
                code: 'MultiSignatureAccountCreated',
                message: 'the account has been created'
            });
            return next();
        }

        sql = `update multisig_account set is_deleted = 1
               where id = ? and is_deleted = 0 and generated_address is null`;
        let result = await mysql.query(sql, [req.params.id]);
        if (result.affectedRows == 0) {
            res.send({
                success: false,
                code: 'MultiSignatureAccountDeleteFailed',
                message: 'delete failed'
            });
            return next();
        }

        // TODO: push message to slaves
        res.send({ success: true });
        next();
    });

    server.post('/multi-signature-account/:accountId/tx', validate('createMultiSignatureTx'), async (req, res, next) => {
        req.checkParams('accountId', 'Not a valid id').isInt();
        req.sanitize('accountId').toInt();

        let errors = req.validationErrors();
        if (errors) {
            return next(new restify.InvalidArgumentError({
                message: errors
            }));
        }

        let accountId = req.params.accountId;
        let status;
        try {
            status = await MultiSig.getAccountStatus(req.token.wid, accountId);
        } catch (err) {
            if (err instanceof restify.HttpError) {
                res.send(err);
            } else {
                log(`get multi-signature-addr by id error, code = ${err.code}, message = ${err.message}`);
                res.send(new restify.InternalServerError('Internal Error'));
            }
            return next();
        }

        if (!status.complete) {
            res.send(new restify.NotFoundError('MultiSignatureAccount not found'));
            return next();
        }

        let { rawtx, note } = req.body;

        let tx;
        try {
            tx = bitcore.Transaction(rawtx);
        } catch (err) {
            res.send({
                success: false,
                code: 'MultiSignatureTxInvalidHex',
                message: err.message
            });
            return next();
        }

        let insertedTxId;

        try {
            await mysql.transaction(async conn => {
                let sql = `insert into multisig_tx
                       (multisig_account_id, hex, complete, note, is_deleted, nonce, created_at, updated_at)
                       values
                       (?, ?, ?, ?, ?, ?, now(), now())`;
                insertedTxId = (await conn.query(sql, [accountId, rawtx, 0, note, 0, 0])).insertId;
                sql = `insert into multisig_tx_participant
                   (multisig_tx_id, wid, status, created_at, updated_at)
                   values
                   (?, ?, ?, now(), now())`;
                await conn.query(sql, [insertedTxId, req.token.wid, 1]);
            });
        } catch (err) {
            res.send({
                success: false,
                code: 'MultiSignatureTxCreateFailed',
                message: 'please try again later'
            });
            return next();
        }

        res.send(_.extend(await MultiSig.getTxStatus(req.token.wid, accountId, insertedTxId), {success: true}));
        next();
    });

    server.get('/multi-signature-account/:accountId/tx/:txId', async (req, res, next) => {
        req.checkParams('accountId', 'Not a valid id').isInt();
        req.sanitize('accountId').toInt();

        req.checkParams('txId', 'Not a valid id').isInt();
        req.sanitize('txId').toInt();

        let errors = req.validationErrors();
        if (errors) {
            return next(new restify.InvalidArgumentError({
                message: errors
            }));
        }

        let accountId = req.params.accountId,
            txId = req.params.txId;
        let accountStatus;
        try {
            accountStatus = await MultiSig.getAccountStatus(req.token.wid, accountId);
        } catch (err) {
            if (err instanceof restify.HttpError) {
                res.send(err);
            } else {
                log(`get multi-signature-addr by id error, code = ${err.code}, message = ${err.message}`);
                res.send(new restify.InternalServerError('Internal Error'));
            }
            return next();
        }

        let txStatus;

        try {
            txStatus = await MultiSig.getTxStatus(req.token.wid, accountId, txId);
        } catch (err) {
            if (err instanceof restify.HttpError) {
                res.send(err);
            } else {
                log(`get multi-signature-addr by id error, code = ${err.code}, message = ${err.message}`);
                res.send(new restify.InternalServerError('Internal Error'));
            }
            return next();
        }

        res.send(_.extend(txStatus, { success: true }));
        next();
    });

    server.put('/multi-signature-account/:accountId/tx/:txId', validate('updateMultiSignatureTx'), async (req, res, next) => {
        req.checkParams('accountId', 'Not a valid id').isInt();
        req.sanitize('accountId').toInt();

        req.checkParams('txId', 'Not a valid id').isInt();
        req.sanitize('txId').toInt();

        let errors = req.validationErrors();
        if (errors) {
            return next(new restify.InvalidArgumentError({
                message: errors
            }));
        }

        let accountId = req.params.accountId,
            txId = req.params.txId;
        let accountStatus;
        try {
            accountStatus = await MultiSig.getAccountStatus(req.token.wid, accountId);
        } catch (err) {
            if (err instanceof restify.HttpError) {
                res.send(err);
            } else {
                log(`get multi-signature-addr by id error, code = ${err.code}, message = ${err.message}`);
                res.send(new restify.InternalServerError('Internal Error'));
            }
            return next();
        }

        let sql = `select * from multisig_tx where id = ? and multisig_account_id = ? limit 1`;
        let tx = await mysql.selectOne(sql, [txId, accountId]);

        if (_.isNull(tx)) {
            res.send(new restify.NotFoundError('MultiSignatureTx not found'));
            return next();
        }

        if (!!tx.complete) {
            res.send({
                success: false,
                code: 'MultiSignatureTxCreated',
                message: 'you are too late'
            });
            return next();
        }

        if (!!tx.is_deleted) {
            res.send({
                success: false,
                code: 'MultiSignatureTxDeleted',
                message: 'you are too late'
            });
            return next();
        }

        if (tx.hex != req.body.original) {
            res.send({
                success: false,
                code: 'MultiSignatureTxHexDismatch',
                message: 'please try again later'
            });
            return next();
        }

        try {
            await mysql.transaction(async conn => {
                let sql = `insert into multisig_tx_participant
                           (multisig_tx_id, wid, status, seq, created_at, updated_at)
                           values
                           (?, ?, ?, ?, now(), now())`;

                await mysql.query(sql, [accountId, req.token.wid, req.body.status, tx.seq + 1]);

                // complete ?
            });
        } catch (err) {

        }
    });

    server.del('/multi-signature-account/:accountId/tx/:txId', async (req, res, next) => {
        req.checkParams('accountId', 'Not a valid id').isInt();
        req.sanitize('accountId').toInt();

        req.checkParams('txId', 'Not a valid id').isInt();
        req.sanitize('txId').toInt();

        let errors = req.validationErrors();
        if (errors) {
            return next(new restify.InvalidArgumentError({
                message: errors
            }));
        }

        let accountId = req.params.accountId,
            txId = req.params.txId;
        let accountStatus;
        try {
            accountStatus = await MultiSig.getAccountStatus(req.token.wid, accountId);
        } catch (err) {
            if (err instanceof restify.HttpError) {
                res.send(err);
            } else {
                log(`get multi-signature-addr by id error, code = ${err.code}, message = ${err.message}`);
                res.send(new restify.InternalServerError('Internal Error'));
            }
            return next();
        }

        let sql = `select * from multisig_tx where id = ? and multisig_account_id = ? limit 1`;
        let tx = await mysql.selectOne(sql, [txId, accountId]);

        if (_.isNull(tx)) {
            res.send(new restify.NotFoundError('MultiSignatureTx not found'));
            return next();
        }

        if (!!tx.complete) {
            res.send({
                success: false,
                code: 'MultiSignatureTxCreated',
                message: 'you are too late'
            });
            return next();
        }

        sql = `update multisig_tx set is_deleted = 1, nonce = ? where id = ? and nonce = ?`;
        let result = await mysql.query(sql, [tx.nonce + 1, tx.id, tx.nonce]);
        if (result.affectedRows == 0) {
            res.send({
                success: false,
                code: 'MultiSignatureDeleteFailed',
                message: 'please try again later'
            });
            return next();
        }

        // TODO: push messages to slaves

        res.send({success: true});
        next();
    });
};