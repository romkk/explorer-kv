let mysql = require('../lib/mysql');
let _ = require('lodash');
let moment = require('moment');
let restify = require('restify');
let validate = require('../lib/valid_json');

module.exports = server => {
    server.post('/mark-notification-message', validate('markNotificationMessage'), async (req, res, next) => {
        let msgIds = req.body.message_ids;

        try {
            let msgs;
            await mysql.transaction(async conn => {
                let sql = `select v1.wid, v2.id, v2.event_type, v2.custom_content, v1.created_at
                                       from notification_message_owner v1
                                            join notification_message v2
                                                 on v1.msg_id = v2.id
                                       where v1.wid = ?
                                       order by v1.created_at asc`;

                msgs = await conn.query(sql, [req.token.wid]);

                // delete marked msgs
                let msgIdsToDelete = msgs.filter(m => msgIds.includes(m.id)).map(r => r.id);
                if (msgIdsToDelete.length) {
                    sql = `delete v1, v2
                               from notification_message_owner as v1 join notification_message as v2
                               where v1.msg_id = v2.id and v1.wid = ? and v1.msg_id in (${ msgIdsToDelete.map(id => '?').join(',') })`;

                    await conn.query(sql, [req.token.wid].concat(msgIdsToDelete));

                    msgs = msgs.filter(m => !msgIdsToDelete.includes(m.id));
                }
            });

            res.send({
                success: true,
                messages: msgs.map(m => {
                    let o = _.omit(m, ['wid', 'created_at']);
                    o.custom_content = JSON.parse(o.custom_content);
                    o.timestamp = moment(m.created_at).unix();
                    return o
                })
            });
        } catch (err) {
            if (err.code && err.message) {
                res.send({
                    success: false,
                    code: err.code,
                    message: err.message
                });
                return next();
            } else {
                res.send(new restify.InternalServerError('Internal Server Error'));
            }
        }
        return next();
    });
};