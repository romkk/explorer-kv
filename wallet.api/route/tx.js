var restify = require('restify');
var validate = require('../lib/valid_json');
var request = require('request-promise');
var config = require('config');
var log = require('debug')('wallet:route:tx');
var _ = require('lodash');
var bitcore = require('bitcore');

module.exports = server => {
    server.post('/tx', validate('tx'), async (req, res, next) => {
        var feePerKB = req.body.fee_per_kb,
            sentFrom = req.body.from,
            sentTo = req.body.to;

        var endpoint = config.get('explorerEndpoint');

        try {
            var response = await request({
                baseUrl: endpoint,
                uri: `/unspent?active=${sentFrom.join('|')}`
            });

            res.send(JSON.parse(response));
        } catch (err) {
            log('request unspent error', err.message);
            res.send(new restify.InternalServerError('Please try again later.'));
        }

        next();
    });

    server.post('/tx/publish', validate('txPublish'), async (req, res, next) => {
        var hex = String(req.body.hex);

        try {
            await request({
                uri: `http://${config.get('bitcoind.host')}:${config.get('bitcoind.port')}`,
                method: 'POST',
                json: { id: 1, method: 'sendrawtransaction', params: [hex] },
                auth: {
                    user: config.get('bitcoind.user'),
                    pass: config.get('bitcoind.pass'),
                    sendImmediately: false
                }
            });

            res.send({
                success: true
            });
        } catch (err) {
            res.send({
                success: false,
                message: err.response.body.error.message || 'Publish failed',
                code: 'TX_PUBLISH_FAILED'
            });
        }

        next();
    });
};