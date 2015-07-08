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
let moment = require('moment');


let formatAccountStatus = (status) => {
    status.participants = status.participants.map(p => {
        p.is_creator = p.role == 'Initiator';
        p.joined_at = moment.utc(p.updated_at).unix();
        return _.omit(p, ['wid', 'role', 'created_at', 'updated_at']);
    });
    status.complete = !_.isNull(status.generated_address);
    return status;
};
let formatTxStatus = status => {
    status.complete = !!status.complete;
    status.is_deleted = !!status.is_deleted;
    status.created_at = moment.utc(status.created_at).unix();
    status.updated_at = moment.utc(status.updated_at).unix();
    status.participants = status.participants.map(p => {
        p = _.omit(p, ['wid']);
        p.joined_at = moment.utc(p.joined_at).unix();
        p.status = ['DENIED', 'APPROVED', 'TBD'][p.status];
        return p;
    });
    return _.omit(status, ['nonce']);
};

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

        if (!PublicKey.isValid(creatorPubkey)) {
            res.send({
                success: false,
                code: 'MultiSignatureAccountInvalidPubkey',
                message: 'Invalid pubkey found'
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

        let result;
        try {
            result = await MultiSig.createAccount(req.token.wid, accountName, m, n, creatorPubkey, creatorName);
        } catch (err) {
            res.send({
                success: false,
                code: 'MultiSignatureAccountCreateFailed',
                message: 'create failed'
            });
            return next();
        }
        let status = await MultiSig.getAccountStatus(result.multiAccountId);
        res.send(_.extend(formatAccountStatus(status), {
            success: true
        }));
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
        let status;
        try {
            status = await MultiSig.getAccountStatus(id);
        } catch (err) {
            res.send(err);
            return next();
        }

        if (status.participants.some(p => p.wid == req.token.wid)) {
            status = formatAccountStatus(status);
            status.success = true;
            res.send(status);
        } else {
            res.send(new restify.NotFoundError('MultiSignatureAccount not found'));
        }
        return next();
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

        if (!(await MultiSig.pubkeyValid(participantPubkey, id))) {
            res.send({
                success: false,
                code: 'MultiSignatureAccountInvalidPubkey',
                message: 'Invalid pubkey found'
            });
            return next();
        }

        // 账户任何人都可以修改，无需鉴别身份
        let status;
        try {
            status = await MultiSig.getAccountStatus(id);
        } catch (err) {
            res.send(err);
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

        if (!_.isNull(status.generated_address) || status.participants.length >= status.m) {
            res.send({
                success: false,
                code: 'MultiSignatureAccountJoinCompleted',
                message: 'you are too late'
            });
            return next();
        }

        try {
            await mysql.transaction(async conn => {
                let newPos = _.last(status.participants).pos + 1;
                let sql, result;
                // 加锁
                sql = `SELECT 1 FROM multisig_account WHERE id = ? GROUP BY 1 FOR UPDATE`;
                await conn.query(sql, [status.id]);

                sql = `insert into multisig_account_participant
                   (multisig_account_id, wid, role, participant_name, pubkey, pos, created_at, updated_at)
                   values
                   (?, ?, ?, ?, ?, ?, now(), now())`;
                await conn.query(sql, [id, req.token.wid, 'Participant', participantName, participantPubkey, newPos]);

                status = await MultiSig.getAccountStatus(id, conn);
                // completed?
                if (status.participants.length < status.m) return;

                // generate new address
                result = await bitcoind('createmultisig', status.n, _.pluck(status.participants, 'pubkey'));
                sql = `update multisig_account set redeem_script = ?, generated_address = ?, updated_at = now()
                           where id = ? and is_deleted = 0`;
                await conn.query(sql, [result.redeemScript, result.address, id]);
                status = await MultiSig.getAccountStatus(id, conn);
            });
        } catch (err) {
            if (err.code == 'ER_DUP_ENTRY') {       // 竞态条件冲突
                res.send({
                    success: false,
                    code: 'MultiSignatureAccountJoinFailed',
                    message: 'please try again later'
                });
                return next();
            } else if (err.name == 'StatusCodeError') {
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
                throw err;
            }
            return next();
        }

        // TODO: push message to master and other slaves
        res.send(_.extend(formatAccountStatus(status), {
            success: true
        }));
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
            status = await MultiSig.getAccountStatus(accountId);
        } catch (err) {
            res.send(err);
            return next();
        }

        if (_.isNull(status.generated_address) || !status.participants.some(p => p.wid == req.token.wid)) {
            res.send(new restify.NotFoundError('MultiSignatureAccount not found'));
            return next();
        }

        let { rawtx, note, complete } = req.body;

        try {
            bitcore.Transaction(rawtx);
        } catch (err) {
            res.send({
                success: false,
                code: 'MultiSignatureTxInvalidHex',
                message: 'invalid hex string'
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
                   (multisig_tx_id, wid, status, seq, created_at, updated_at)
                   values
                   ${ status.participants.map(p => `(?, ?, ?, ?, now(), now())`).join(',') }`;
                let params = [];
                let pos = 0;
                status.participants.forEach((p, i) => params.push(insertedTxId, p.wid, p.wid == req.token.wid ? 1 : 2, pos++));
                await conn.query(sql, params);
            });
        } catch (err) {
            res.send({
                success: false,
                code: 'MultiSignatureTxCreateFailed',
                message: 'please try again later'
            });
            return next();
        }

        if (complete) {
            let txHash = await MultiSig.sendMultiSigTx(rawtx, insertedTxId);
            if (txHash == false) {
                // 删除新建的交易
                let sql = `update multisig_tx set is_deleted = ?, updated_at = ? where id = ?`;
                await mysql.query(sql, [1, moment.utc().format('YYYY-MM-DD HH:mm:ss'), insertedTxId]);
                res.send({
                    success: false,
                    code: 'MultiSignatureTxPublishFailed',
                    message: 'incomplete signature ? '
                });
                return next();
            }
        }

        // TODO send message to other participants
        res.send(_.extend(formatTxStatus(await MultiSig.getTxStatus(accountId, insertedTxId)), {success: true}));
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
            accountStatus = await MultiSig.getAccountStatus(accountId);
            if (!accountStatus.participants.some(p => p.wid == req.token.wid)) {
                throw new restify.NotFoundError('MultiSignatureAccount not found');
            }
        } catch (err) {
            res.send(err);
            return next();
        }

        let txStatus;

        try {
            txStatus = await MultiSig.getTxStatus(accountId, txId);
            if (!txStatus.participants.some(p => p.wid == req.token.wid)) {
                throw new restify.NotFoundError('MultiSignatureTx not found');
            }
        } catch (err) {
            res.send(err);
            return next();
        }

        res.send(_.extend(formatTxStatus(txStatus), { success: true }));
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
            accountStatus = await MultiSig.getAccountStatus(accountId);
            if (!accountStatus.participants.some(p => p.wid == req.token.wid)) {
                throw new restify.NotFoundError('MultiSignatureAccount not found');
            }
        } catch (err) {
            res.send(err);
            return next();
        }

        try {
            await mysql.transaction(async conn => {
                let sql = `select * from multisig_tx where id = ? and multisig_account_id = ? limit 1 for update`;
                let tx = await conn.selectOne(sql, [txId, accountId]);

                if (_.isNull(tx)) {
                    let e = new Error();
                    e.code = 'NotFoundError';
                    e.message = 'MultiSignatureTx not found';
                    throw e;
                }

                if (!!tx.complete) {
                    let e = new Error();
                    e.code = 'MultiSignatureTxCreated';
                    e.message = 'you are too late';
                    throw e;
                }

                if (!!tx.is_deleted) {
                    let e = new Error();
                    e.code = 'MultiSignatureTxDeleted';
                    e.message = 'you are too late';
                    throw e;
                }

                if (tx.hex != req.body.original) {
                    let e = new Error();
                    e.code = 'MultiSignatureTxHexDismatch';
                    e.message = 'tx hex dismatch';
                    throw e;
                }

                sql = `update multisig_tx set hex = ?, updated_at = ? where id = ?;
                       update multisig_tx_participant set status = ? where multisig_tx_id = ? and wid = ?`;
                await conn.query(sql, [ req.body.signed, moment.utc().format('YYYY-MM-DD HH:mm:ss'), txId,
                    ({ 'APPROVED': 1, 'DENIED': 0 })[req.body.status],
                    txId, req.token.wid]);
            });

        } catch (err) {
            if (err.code && err.message) {
                if (err.code.endsWith('Error')) {
                    res.send(new restify[err.code](err.message));
                } else {
                    res.send({
                        success: false,
                        code: err.code,
                        message: err.message
                    });
                }
                return next();
            } else {
                throw err;
            }
        }

        if (req.body.complete) {
            let txHash = await MultiSig.sendMultiSigTx(req.body.signed, txId);
            if (txHash == false) {
                // 删除新建的交易
                let sql = `update multisig_tx set is_deleted = ?, updated_at = ? where id = ?`;
                await mysql.query(sql, [1, moment.utc().format('YYYY-MM-DD HH:mm:ss'), txId]);

                // TODO send fail message to every one

                res.send({
                    success: false,
                    code: 'MultiSignatureTxPublishFailed',
                    message: 'incomplete signature ? '
                });
                return next();
            }

            // TODO send successful message to every one
        }

        res.send(_.extend(formatTxStatus(await MultiSig.getTxStatus(accountId, insertedTxId)), {success: true}));
        return next();
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
            accountStatus = await MultiSig.getAccountStatus(accountId);
            if (!accountStatus.participants.some(p => p.wid == req.token.wid)) {
                throw new restify.NotFoundError('MultiSignatureAccount not found');
            }
        } catch (err) {
            res.send(err);
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