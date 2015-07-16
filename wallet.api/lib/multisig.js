let mysql = require('./mysql');
let _ = require('lodash');
let PublicKey = require('bitcore').PublicKey;
let restify = require('restify');
let log = require('debug')('wallet:lib:multisig');
let moment = require('moment');
let bitcoind = require('./bitcoind');

class MultiSig {

    static async accountParamsValid(id, pubKey = null, wid = null, name = null) {
        if (pubKey == null && wid == null && name == null) {
            return false;
        }

        if (!PublicKey.isValid(pubKey)) {
            return false;
        }

        let condition = [];
        if (pubKey) condition.push('pubkey = ?');
        if (wid) condition.push('wid = ?');
        if (name) condition.push('participant_name = ?');

        let sql = `select id from multisig_account_participant
                   where multisig_account_id = ? and (${condition.join(' or ')})
                   limit 1`;
        let row = await mysql.selectOne(sql, [id].concat(_.compact([pubKey, wid, name])));
        return _.isNull(row);
    }

    static async createAccount(wid, accountName, m, n, creatorPubKey, creatorName) {
        let multiAccountId, multiAccountParticipantId;

        /**
         * 如有异常，直接抛出
         */
        await mysql.transaction(async conn => {
            let accountSql = `insert into multisig_account
                                    (account_name, m, n, is_deleted, created_at, updated_at)
                              values
                                    (?, ?, ?, 0, now(), now())`;
            let result = await conn.query(accountSql, [accountName, m, n]);
            multiAccountId = result.insertId;
            let participantSql = `insert into multisig_account_participant
                                    (multisig_account_id, wid, role, participant_name, pubkey, pos, created_at, updated_at)
                                    values
                                    (?, ?, ?, ?, ?, ?, now(), now())`;
            result = await conn.query(participantSql, [multiAccountId, wid, 'Initiator', creatorName, creatorPubKey, 0]);
            multiAccountParticipantId = result.insertId;
        });
        return {
            multiAccountId: multiAccountId,
            multiAccountParticipantId: multiAccountParticipantId
        };
    }

    static async getAccountStatus(id, conn = mysql) {
        let sql = `select v1.id, v1.account_name, v1.m, v1.n, v1.generated_address, v1.redeem_script,
                          v2.wid, v2.role, v2.participant_name, v2.pubkey, v2.pos, v2.created_at, v2.updated_at
                   from multisig_account v1
                     join multisig_account_participant v2
                       on v1.id = v2.multisig_account_id
                   where v1.id = ? and v1.is_deleted = 0
                   order by v2.pos asc`;

        let account = await conn.query(sql, [id]);

        if (!account.length) {
            throw new restify.NotFoundError('MultiSignatureAccount not found');
        }

        let o = _.pick(account[0], ['id', 'account_name', 'm', 'n', 'generated_address', 'redeem_script']);
        o.participants = account.map(a => _.pick(a, ['wid', 'role', 'participant_name', 'pubkey', 'pos', 'created_at', 'updated_at']));

        return o;
    }

    static async getTxStatus(accountId, txId, conn = mysql) {
        let sql = `select v1.*, v2.wid, v2.status as participant_status, v2.seq, v2.updated_at as joined_at, v3.participant_name
                   from multisig_tx v1
                     join multisig_tx_participant v2
                       on v1.id = v2.multisig_tx_id
                     join (select multisig_account_id, participant_name, wid
                           from multisig_account_participant
                           where multisig_account_id = ?) v3
                       on v1.multisig_account_id = v3.multisig_account_id and v2.wid = v3.wid
                   where v1.multisig_account_id = ? and v1.id = ?
                   order by v2.seq asc`;
        let rows = await conn.query(sql, [accountId, accountId, txId]);
        if (!rows.length) {
            throw new restify.NotFoundError('MultiSignatureTransaction not found');
        }

        let o = _.pick(rows[0], ['hex', 'id', 'multisig_account_id', 'note', 'status', 'note', 'is_deleted', 'nonce', 'created_at', 'updated_at']);
        o.participants = rows.map(r => _.pick(r, ['participant_status', 'seq', 'wid', 'joined_at', 'participant_name']));

        return o;
    }

    static async sendMultiSigTx(rawhex, txId, conn = mysql) {
        let txHash;
        try {
            txHash = await bitcoind('sendrawtransaction', rawhex);
        } catch (err) {
            if (err.response.body.error.code == -25 || err.response.body.error.code == -22) {
                return false;
            } else {
                throw err;
            }
        }
        let sql = `update multisig_tx set hex = ?, nonce = nonce + 1, updated_at = ?, status = 1 where id = ?`;
        await conn.query(sql, [rawhex, 1, moment.utc().format('YYYY-MM-DD HH:mm:ss'), txId]);
        return txHash;
    }
}

module.exports = MultiSig;