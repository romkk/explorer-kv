let mysql = require('./mysql');
let _ = require('lodash');
let PublicKey = require('bitcore').PublicKey;
let restify = require('restify');
let log = require('debug')('wallet:lib:multisig');
let moment = require('moment');
let bitcoind = require('./bitcoind');

class MultiSig {

    static async pubkeyValid(pubKey, name) {
        if (!PublicKey.isValid(pubKey)) {
            return false;
        }
        let sql = `select id from multisig_account_participant where pubkey = ? limit 1`;
        let row = await mysql.selectOne(sql, [pubKey]);
        return _.isNull(row);
    }

    static async participantNameValid(id, name) {
        let sql = `select id from multisig_account_participant
                   where multisig_account_id = ? and participant_name = ? limit 1`;
        let row = await mysql.selectOne(sql, [id, name]);
        return _.isNull(row);
    }

    static async createAccount(wid, accountName, m, n, creatorPubKey, creatorName) {
        let multiAccountId, multiAccountParticipantId;

        /**
         * 如有异常，直接抛出
         */
        await mysql.transaction(async conn => {
            let accountSql = `insert into multisig_account (wid, account_name, m, n, created_at, updated_at) values (?, ?, ?, ?, now(), now())`;
            let result = await conn.query(accountSql, [wid, accountName, m, n]);
            multiAccountId = result.insertId;
            let participantSql = `insert into multisig_account_participant
                                    (multisig_account_id, role, participant_name, pubkey, pos, created_at, updated_at)
                                    values
                                    (?, ?, ?, ?, ?, now(), now())`;
            result = await conn.query(participantSql, [multiAccountId, 'Initiator', creatorName, creatorPubKey, 0]);
            multiAccountParticipantId = result.insertId;
        });
        return {
            multiAccountId: multiAccountId,
            multiAccountParticipantId: multiAccountParticipantId
        };
    }

    static async getAccountStatus(wid, id) {
        let sql = `select id, account_name, m, n, generated_address, redeem_script
                   from multisig_account
                   where wid = ? and id = ? limit 1`;

        let account = await mysql.selectOne(sql, [wid, id]);

        if (_.isNull(account)) {
            throw new restify.NotFoundError('MultiSignatureAccount not found');
        }

        let o = _.extend({}, account, {
            complete: !_.isNull(account.generated_address)
        });

        sql = `select * from multisig_account_participant
               where multisig_account_id = ?
               order by pos asc`;

        let participants = await mysql.query(sql, [id]);

        o.participants = participants.map(p => {
            return {
                is_creator: p.role == 'Initiator',
                name: p.participant_name,
                pubkey: p.pubkey,
                pos: p.pos,
                joined_at: moment(p.created_at).unix()
            };
        });

        return o;
    }

    static async joinAccount(id, name, pubkey, newPos) {
        let sql = `insert into multisig_account_participant
                   (multisig_account_id, role, participant_name, pubkey, pos, created_at, updated_at)
                   values
                   (?, ?, ?, ?, ?, now(), now())`;
        //竞态条件冲突的异常抛出
        let result = await mysql.query(sql, [id, 'Participant', name, pubkey, newPos]);
        return result.insertId;
    }
}

module.exports = MultiSig;