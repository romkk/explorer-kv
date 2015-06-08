var config = require('config');
var ssdb = require('ssdb');
var _ = require('lodash');

var conf = config.get('ssdb');

_.extend(conf, {
    size: 10,
    timeout: 0,
    promisify: true,
    trunkify: false
});

var pool = ssdb.createPool(conf);

module.exports = pool.acquire.bind(pool);