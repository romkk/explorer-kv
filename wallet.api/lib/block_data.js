var request = require('request-promise');
var qs = require('querystring');
var restify = require('restify');
var config = require('config');
var endpoint = config.get('explorerEndpoint');
var log = require('debug')('wallet:lib/block_data');
var _ = require('lodash');

module.exports = async (path, querySet) => {
    var querystring = _.isPlainObject(querySet) ? qs.stringify(querySet) : querySet;

    log(`request block chain data api, path = ${path}, qs = ${querystring}`);

    try {
        var result = await request({
            baseUrl: endpoint,
            uri: `${path}?${querystring}`,
            timeout: 10000
        });
        return JSON.parse(result);
    } catch (err) {
        log(`request block chain data api error: ${err.message}, path = ${path}, qs = ${querystring}`);
        throw new restify.InternalServerError('Please try again later.');
    }
};