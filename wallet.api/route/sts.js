let STSToken = require('../lib/sts');
let restify = require('restify');
let mysql = require('../lib/mysql');
let _ = require('lodash');

module.exports = server => {
    server.get('/sts-token', async (req, res, next) => {
        let wid = req.token.wid;

        // get wid_internal
        let sql = `select id from wallet where wid = ?`;
        let widInternal = await mysql.pluck(sql, 'id',[wid]);

        let result;
        try {
            result = await STSToken.make(widInternal, wid);
        } catch (err) {
            console.log(err.stack);
            res.send(new restify.InternalServerError('Internal Error'));
            return next();
        }

        res.send(_.pick(result, ['Credentials', 'FederatedUser']));
        return next();
    });
};