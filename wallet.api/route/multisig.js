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
    status.status = ['DENIED', 'APPROVED', 'TBD'][status.status];
    status.is_deleted = !!status.is_deleted;
    status.created_at = moment.utc(status.created_at).unix();
    status.updated_at = moment.utc(status.updated_at).unix();
    status.participants = status.participants.map(p => {
        let o = _.omit(p, ['wid', 'participant_status', 'role']);
        o.joined_at = moment.utc(p.joined_at).unix();
        o.status = ['DENIED', 'APPROVED', 'TBD'][p.participant_status];
        o.is_creator = p.role === 'Initiator';
        return o;
    });
    return _.omit(status, ['nonce', 'role']);
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

        status = formatAccountStatus(status);
        status.success = true;
        res.send(status);
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

        if (!(await MultiSig.accountParamsValid(id, participantPubkey, req.token.wid, participantName))) {
            res.send({
                success: false,
                code: 'MultiSignatureAccountInvalidParams',
                message: 'invalid/duplicate pubkey, wid, or participant name'
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
                           where id = ?`;
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
                    description: _.get(err, 'response.body.error'),
                    message: 'create multisig failed',
                    code: 'MultiSignatureAccountJoinFailed'
                });
                await mysql.query(`update multisig_account set is_deleted = 1, updated_at = now() where id = ?`, [id]);
                //TODO push message to all
            } else {
                console.log(err);
                res.send(new restify.InternalServerError('Internal Error'));
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

        let id = req.params.id;

        // get status
        let status;
        try {
            status = await MultiSig.getAccountStatus(id);
        } catch (err) {
            res.send(err);
            return next();
        }

        let p = status.participants[0];

        if (p.wid != req.token.wid) {
            res.send({
                success: false,
                message: 'permission denied',
                code: 'MultiSignatureAccountDenied'
            });
            return next();
        }

        if (!!status.generated_address) {
            res.send({
                success: false,
                code: 'MultiSignatureAccountCreated',
                message: 'the account has been created'
            });
            return next();
        }

        let sql = `update multisig_account set is_deleted = 1
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

    /**
     * 获取多重签名账户交易列表
     */
    server.get('/multi-signature-account/:accountId/tx', async (req, res, next) => {
        req.checkParams('accountId', 'Not a valid id').isInt();
        req.sanitize('accountId').toInt();

        req.checkQuery('offset', 'should be a valid number').optional().isNumeric();
        req.sanitize('offset').toInt();

        req.checkQuery('limit', 'should be between 1 and 50').optional().isNumeric().isInt({ max: 50, min: 1});
        req.sanitize('limit').toInt();

        req.checkQuery('sort', 'should be desc or asc').optional().isIn(['desc', 'asc']);

        let errors = req.validationErrors();
        if (errors) {
            return next(new restify.InvalidArgumentError({
                message: errors
            }));
        }

        let offset = _.get(req, 'params.offset', 0);
        let limit = _.get(req, 'params.limit', 50);

        let accountId = req.params.accountId;
        let accountStatus;
        try {
            accountStatus = await MultiSig.getAccountStatus(accountId);
        } catch (err) {
            res.send(err);
            return next();
        }

        if (_.isNull(accountStatus.generated_address) || !accountStatus.participants.some(p => p.wid == req.token.wid)) {
            res.send(new restify.NotFoundError('MultiSignatureAccount not found'));
            return next();
        }

        let ret = [];
        let failedTxIter = MultiSig.getAccountUnApprovedTx(accountId);
        let successTxIter = MultiSig.getMultiAccountTxList(accountStatus.generated_address);

        for (let i = failedTxIter.next(), j = successTxIter.next();;) {
            if (i.done && j.done) {
                break;
            } else if (i.done && !j.done) {
                try {
                    ret.push(await j.value);
                } catch (e) {
                    continue;
                } finally {
                    j = successTxIter.next();
                }
            } else if (!i.done && j.done) {
                try {
                    ret.push(await i.value);
                } catch (e) {
                    continue;
                } finally {
                    i = failedTxIter.next();
                }
            } else {
                let ii, jj;
                try {
                    ii = await i.value;
                } catch (e) {
                    i = failedTxIter.next();
                    continue;
                }

                try {
                    jj = await j.value;
                } catch (e) {
                    j = successTxIter.next();
                    continue;
                }

                if (ii.timestamp >= jj.time) {
                    ret.push(ii);
                    i = failedTxIter.next();
                } else {
                    ret.push(jj);
                    j = successTxIter.next();
                }
            }
            if (ret.length >= offset + limit) break;
        }

        ret = ret.slice(offset, offset + limit);

        let hashList = _.pluck(ret.filter(r => !_.isUndefined(r.is_coinbase)), 'hash');
        let result = {};
        if (hashList.length) {
            let sql = `select * from multisig_tx where txhash in (${_.fill(new Array(hashList.length), '?').join(', ')})`;
            result = await mysql.query(sql, _.keys(hashList));
            result = _.indexBy(result, 'txhash');
        }

        ret = _.compact(ret.map(r => {
            if (_.isUndefined(r.is_coinbase)) {
                return r;
            }

            let p = result[r.hash];
            if (!p) {
                return {
                    id: null,
                    note: null,
                    txhash: r.hash,
                    timestamp: r.time,
                    status: 'RECEIVED',
                    amount: 2,
                    inputs: _(r.inputs.map(i => _.get(i, 'prev_out.addr', []))).flatten().compact().uniq().value(),
                    outputs: _(r.out.map(i => _.get(i, 'addr', []))).flatten().compact().uniq().value()
                };
            }

            let o = _.pick(p, ['id', 'note', 'txhash']);
            o.timestamp = r.time;
            o.status = ['DENIED', 'APPROVED', 'TBD'][p.status] || 'RECEIVED';
            o.amount = 1;
            o.inputs = _(r.inputs.map(i => _.get(i, 'prev_out.addr', []))).flatten().compact().uniq().value();
            o.outputs = _(r.out.map(i => _.get(i, 'addr', []))).flatten().compact().uniq().value();
            return o;
        }));

        res.send(ret);
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
                       (multisig_account_id, hex, status, note, is_deleted, nonce, created_at, updated_at)
                       values
                       (?, ?, ?, ?, ?, ?, now(), now())`;
                insertedTxId = (await conn.query(sql, [accountId, rawtx, 2, note, 0, 0])).insertId;
                sql = `insert into multisig_tx_participant
                   (multisig_tx_id, role, wid, status, seq, created_at, updated_at)
                   values
                   ${ status.participants.map(p => `(?, ?, ?, ?, ?, now(), now())`).join(',') }`;
                let params = [];
                let pos = 0;
                status.participants.forEach((p, i) => params.push(insertedTxId, p.wid == req.token.wid ? 'Initiator' : 'Participant', p.wid, p.wid == req.token.wid ? 1 : 2, pos++));
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

        if (req.body.complete) {
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

        if (req.body.status == 'APPROVED' &&
            (_.isUndefined(req.body.signed) || _.isUndefined(req.body.complete))) {
            res.send({
                success: false,
                code: 'MultiSignatureTxInvalidParams',
                message: 'invalid params'
            });
            return next();
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
            if (txStatus.status != 2) {
                throw {
                    success: false,
                    code: 'MultiSignatureTxStatusFixed',
                    message: 'the tx has been done'
                };
            }
            if (txStatus.participants.some(p => p.wid == req.token.wid && p.participant_status != 2)) {
                throw {
                    success: false,
                    code: 'MultiSignatureTxStatusFixed',
                    message: 'you have approved or denied this tx'
                };
            }
        } catch (err) {
            res.send(err);
            return next();
        }

        try {
            bitcore.Transaction(req.body.signed);
        } catch (err) {
            res.send({
                success: false,
                code: 'MultiSignatureTxInvalidHex',
                message: 'invalid hex string'
            });
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

                if (tx.status != 2) {
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

                if (req.body.status == 'APPROVED') {
                    sql = `update multisig_tx set hex = ?, updated_at = ? where id = ?;`;
                    await conn.query(sql, [req.body.signed, moment.utc().format('YYYY-MM-DD HH:mm:ss'), txId]);
                }

                sql = `update multisig_tx_participant set status = ? where multisig_tx_id = ? and wid = ?`;
                await conn.query(sql, [({'APPROVED': 1, 'DENIED': 0 })[req.body.status], txId, req.token.wid]);

                // 是否该交易已被审批为失败
                sql = `select count(id) as cnt from multisig_tx_participant where status = 0 and multisig_tx_id = ?`;
                let failed = await conn.pluck(sql, 'cnt', [txId]);
                if (failed > accountStatus.m - accountStatus.n) {
                    // TODO send fail message to every one
                    sql = `update multisig_tx set nonce = nonce + 1, updated_at = ?, status = 0 where id = ?`;
                    await conn.query(sql, [moment.utc().format('YYYY-MM-DD HH:mm:ss'), txId]);
                } else if (req.body.complete) {
                    let txHash = await MultiSig.sendMultiSigTx(req.body.signed, txId, conn);
                    if (txHash == false) {
                        // TODO send fail message to every one
                        throw {
                            success: false,
                            code: 'MultiSignatureTxPublishFailed',
                            message: 'incomplete signature ? '
                        };
                    }
                }
            });

        } catch (err) {
            // 删除新建的交易
            let sql = `update multisig_tx set is_deleted = 1, nonce = nonce + 1, updated_at = ? where id = ?`;
            await mysql.query(sql, [moment.utc().format('YYYY-MM-DD HH:mm:ss'), txId]);

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
            } else {
                console.log(err);
                res.send(new restify.InternalServerError('Internal Error'));
            }
            return next();
        }

        // TODO send successful message to every one
        res.send(_.extend(formatTxStatus(await MultiSig.getTxStatus(accountId, txId)), {success: true}));
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

        let txStatus;
        try {
            txStatus = await MultiSig.getTxStatus(accountId, txId);
            if (!txStatus.participants.some(p => p.wid == req.token.wid)) {
                throw new restify.NotFoundError('MultiSignatureTx not found');
            }
            if (txStatus.status != 2) {
                throw {
                    success: false,
                    code: 'MultiSignatureTxCreated',
                    message: 'you are too late'
                };
            }
            if (txStatus.participants.some(p => p.wid == req.token.wid && p.role != 'Initiator')) {
                throw {
                    success: false,
                    code: 'MultiSignatureTxStatusFixed',
                    message: 'permission denied'
                };
            }
        } catch (err) {
            res.send(err);
            return next();
        }

        let sql = `update multisig_tx set is_deleted = 1, nonce = nonce + 1 where id = ? and nonce = ?`;
        let result = await mysql.query(sql, [txStatus.id, txStatus.nonce]);
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