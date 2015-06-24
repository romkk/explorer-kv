var request = require('request-promise');
var config = require('config');
var log = require('debug')('wallet:lib:bitcoind');

var requestId = 0;

module.exports = async (method, ...params) => {
    log(`发起 RPC 调用，${method}(${params.toString()})`);
    var start = Date.now();
    try {
        var response = await request({
            uri: `http://${config.get('bitcoind.host')}:${config.get('bitcoind.port')}`,
            method: 'POST',
            json: { id: requestId++, method: method, params: params },
            auth: {
                user: config.get('bitcoind.user'),
                pass: config.get('bitcoind.pass'),
                sendImmediately: false
            }
        });
        log(`RPC 调用完成: ${Date.now() - start} ms`);
        return JSON.parse(response);
    } catch (err) {
        log(`RPC 调用失败，message = ${err.message}, err = ${err.name}`);
        throw err;
    }
};