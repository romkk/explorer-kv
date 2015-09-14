let mysql = require('../lib/mysql');
let _ = require('lodash');
let restify = require('restify');
let moment = require('moment');

module.exports = server => {
    server.get('latestversion', async (req, res, next) => {
        req.checkParams('lang', 'can not be empty').isLength(1);
        req.sanitize('lang').toString();

        req.checkParams('channel', 'can not be empty').isLength(1);
        req.sanitize('channel').toString();

        let errors = req.validationErrors();
        if (errors) {
            return next(new restify.InvalidArgumentError({
                message: errors
            }));
        }

        let sql = `select version, lang, release_note, download_url, released_at
                   from autoupdate_release
                   where lang = ?
                   order by released_at desc
                   limit 1`;     // 暂时忽略渠道

        let v = await mysql.selectOne(sql, [req.params.lang]);

        if (_.isNull(v)) {
            res.send({
                release_version: '0.0',
                release_note: '',
                release_download: '',
                release_date: ''
            });
            return next();
        }

        res.send({
            release_version: v.version,
            release_note: v.release_note,
            release_download: v.download_url,
            release_date: moment.utc(v.release_date, 'YYYY-MM-DD HH:mm:ss').unix()
        });
        next();
    });
};