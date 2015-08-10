let bitcoind = require('../lib/bitcoind');
let _ = require('lodash');
let restify = require('restify');
var validate = require('../lib/valid_json');

module.exports = server => {
    server.post('/tx/decode', validate('txPublish'), async (req, res, next) => {
        var hex = String(req.body.hex);

        try {
            let tx = await bitcoind('decoderawtransaction', hex);
            res.send({
                success: true,
                decoded_tx: tx
            });
        } catch (err) {
            if (err.name == 'StatusCodeError') {
                res.send({
                    success: false,
                    description: _.get(err, 'response.body.error.message', null),
                    message: 'Publish failed',
                    code: 'TX_PUBLISH_FAILED'
                });
            } else {
                res.send(new restify.InternalServerError('RPC call error.'));
            }
        }

        next();
    });

    server.post('/tx/publish', validate('txPublish'), async (req, res, next) => {
        var hex = String(req.body.hex);

        try {
            let txHash = await bitcoind('sendrawtransaction', hex);
            res.send({
                success: true,
                tx_hash: txHash
            });
        } catch (err) {
            if (err.name == 'StatusCodeError') {
                res.send({
                    success: false,
                    description: _.get(err, 'response.body.error.message', null),
                    message: 'Publish failed',
                    code: 'TX_PUBLISH_FAILED'
                });
            } else {
                res.send(new restify.InternalServerError('RPC call error.'));
            }
        }

        next();
    });
};