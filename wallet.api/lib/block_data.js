var request = require('request-promise');
var qs = require('querystring');
var restify = require('restify');
var config = require('config');
var endpoint = config.get('explorerEndpoint');
var log = require('debug')('wallet:lib/block_data');
var _ = require('lodash');

module.exports = async (path, querySet = {}) => {
    if (typeof querySet == 'string') {
        querySet = qs.parse(querySet);
    }

    log(`request block chain data api, path = ${path}, qs = ${qs.stringify(querySet)}`);

    try {
        return await request({
            baseUrl: endpoint,
            uri: path,
            qs: querySet,
            timeout: 10000,
            json: true
        });
    } catch (err) {
        log(`request block chain data api error: ${err.message}, path = ${path}, qs = ${qs.stringify(querySet)}`);
        throw err;
    }
};