let request = require('request-promise');
let log = require('debug')('wallet:lib:usercenter');
let config = require('config');

module.exports = {
    async get(ticket) {
        let response;
        log(`验证 ticket(${ticket})`);
        try {
            response = await request({
                uri: config.get('userCenter'),
                qs: {
                    ticket: ticket,
                    reqType: 1
                },
                strictSSL: false
            });
            response = JSON.parse(response);
        } catch (err) {
            log(`与用户中心服务器通信失败，${err.stack}`);
            return false;
        }

        let valid = response.code == '0';

        log(`令牌${valid ? '合法' : '不合法'}`);

        if (!valid) return false;

        return response;
    }
};