var mysql = require('../lib/mysql');
var log = require('debug')('wallet:route:ping');

module.exports = server => {

    server.get('/ping', async (req, res, next) => {
        var start = Date.now();

        try {
            await mysql.query("select 1 + 1");
        } catch (err) {
            log('访问 mysql 失败');
            res.send(500, {
                success: false,
                message: err.message,
                name: err.name,
                stack: err.stack
            });
        }

        var total = Date.now() - start;

        if (total > 10000) {
            log(`响应时间过长, ${total}ms`);
            res.send(500, {
                success: false,
                message: `total ${total}ms, tooooo long`
            });
            return next();
        }

        log(`pong -> ${total}ms`);

        res.send({
            success: true
        });

        next();
    });

};