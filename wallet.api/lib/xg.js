let _ = require('lodash');
let moment = require('moment');
let config = require('config');
let xinge = require('../vendor/Xinge');
let log = require('debug')('wallet:lib:xg');
let mysql = require('./mysql');

class XG {
    static make() {
        if (!XG.instance) {
            XG.instance = new XG();
        }
        return XG.instance;
    }

    static async send(...args) {
        let xg = XG.make();
        return xg.sendMessage.apply(xg, args);
    }

    constructor() {
        this.androidXG = new xinge.XingeApp(+config.get('xg.android.accessId'), config.get('xg.android.secretKey'));
        this.iOSXG = new xinge.XingeApp(+config.get('xg.ios.accessId'), config.get('xg.ios.secretKey'));
    }

    async sendMessage(receivers, eventType, args, extraKV = {}) {
        if (Array.isArray(receivers)) {
            if (!receivers.length) {
                log('receivers 为空，处理结束');
                return true;
            }
        } else {
            receivers = [receivers];
        }
        
        if (!Array.isArray(args)) {
            args = [args];
        }

        let msgId = await this.save(receivers, eventType, extraKV);
        extraKV.wallet_msg_id = msgId;

        let eventName = eventType.slice(eventType.indexOf('EVENT_') + 'EVENT_'.length);
        extraKV = _.mapValues(extraKV, v => String(v));

        // prepare Android
        let androidMessage = new xinge.AndroidMessage();
        androidMessage.content = eventName;
        androidMessage.expireTime = 259200;
        androidMessage.type = xinge.MESSAGE_TYPE_MESSAGE;
        _.extend(androidMessage.customContent, {
            action: eventName,
            args: args.join('|')
        }, extraKV);

        // prepare iOS
        let iOSMessage = new xinge.IOSMessage();
        iOSMessage.expireTime = 259200;
        _.extend(iOSMessage.customContent, {
            action: eventName,
            args: args
        }, extraKV);
        iOSMessage.badge = 1;
        iOSMessage.sound = 'default';
        iOSMessage.alert = {
            'loc-key': eventName,
            'loc-args': args
        };

        let androidPromise = new Promise((resolve, reject) => {
            this.androidXG.pushByAccounts(receivers, androidMessage, null, (e, data) => {
                if (e) return reject(e);
                resolve(data);
            });
        });

        let iOSPromise = new Promise((resolve, reject) => {
            this.iOSXG.pushByAccounts(receivers, iOSMessage, process.env.IOS_ENV && xinge[process.env.IOS_ENV] || xinge.IOS_ENV_DEV, (e, data) => {
                if (e) return reject(e);
                resolve(data);
            });
        });

        // push message
        log(`push message, receivers = ${receivers.join(',')}, eventType = ${eventType}, args = ${args.join(',')}, extraKV = ${JSON.stringify(extraKV)}`);

        let [iOSResult, androidResult] = await Promise.settle([iOSPromise, androidPromise]);
        if (iOSResult.isFulfilled()) {
            let r = JSON.parse(iOSResult.value());
            log(`push ios success, code = ${r.ret_code}, result = ${r.result}`);
        } else if (iOSResult.isRejected()) {
            log(`push ios failed`);
            console.log(iOSResult.reason().stack);
        }

        if (androidResult.isFulfilled()) {
            let r = JSON.parse(androidResult.value());
            log(`push android success, code = ${r.ret_code}, result = ${r.result}`);
        } else if (androidResult.isRejected()) {
            log(`push android failed`);
            console.log(androidResult.reason().stack);
        }
    }

    async save(receivers, eventType, customContent) {
        try {
            let msgId;
            await mysql.transaction(async conn => {
                let sql = `insert into notification_message
                           (event_type, custom_content, created_at, updated_at)
                           values
                           (?, ?, now(), now())`;
                let result = await conn.query(sql, [eventType, JSON.stringify(customContent)]);
                msgId = result.insertId;

                sql = `insert into notification_message_owner
                       (wid, msg_id, created_at, updated_at)
                       values
                       ${receivers.map(r => '(?, ?, now(), now())').join(', ')}`;
                await conn.query(sql, _.flatten(receivers.map(r => [ r, msgId ])));
            });
            return msgId;
        } catch (err) {
            log(`插入 mssage 队列失败， eventType = ${eventType}, receivers = ${receivers}`);
            throw err;
        }
    }
}

XG.instance = null;

XG.EVENT_MULTISIG_ACCOUNT_CHANGE = 'EVENT_MULTISIG_ACCOUNT_CHANGE';
XG.EVENT_MULTISIG_ACCOUNT_CREATED = 'EVENT_MULTISIG_ACCOUNT_CREATED';
XG.EVENT_MULTISIG_ACCOUNT_DELETE = 'EVENT_MULTISIG_ACCOUNT_DELETE';
XG.EVENT_MULTISIG_ACCOUNT_CREATE_FAILED = 'EVENT_MULTISIG_ACCOUNT_CREATE_FAILED';
XG.EVENT_MULTISIG_TX_CREATE = 'EVENT_MULTISIG_TX_CREATE';
XG.EVENT_MULTISIG_TX_CHANGE_APPROVED_APPROVED = 'EVENT_MULTISIG_TX_CHANGE_APPROVED_APPROVED';
XG.EVENT_MULTISIG_TX_CHANGE_APPROVED_TBD = 'EVENT_MULTISIG_TX_CHANGE_APPROVED_TBD';
XG.EVENT_MULTISIG_TX_CHANGE_DENIED_DENIED = 'EVENT_MULTISIG_TX_CHANGE_DENIED_DENIED';
XG.EVENT_MULTISIG_TX_CHANGE_DENIED_TBD = 'EVENT_MULTISIG_TX_CHANGE_DENIED_TBD';
XG.EVENT_MULTISIG_TX_DELETE = 'EVENT_MULTISIG_TX_DELETE';

module.exports = XG;