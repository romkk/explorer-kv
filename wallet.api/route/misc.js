var sb = require('../lib/ssdb')();
var request = require('request-promise');
var _ = require('lodash');
var restify = require('restify');
var log = require('debug')('wallet:ticker');

module.exports = (server) => {
    server.get('/ticker', async (req, res, next) => {
        const TICKER_CACHE ='wallet_ticker';
        const TICKER_TIMER = 'wallet_ticker_lastupdate';
        const UPSTREAM = 'https://blockchain.info/ticker';

        function cacheExists() {
            return cache[TICKER_CACHE] != null && cache[TICKER_TIMER] != null;
        }

        function cacheValid() {
            return cache[TICKER_TIMER] && Date.now() - cache[TICKER_TIMER] <= 15 * 60 * 1000;
        }

        async function pull() {
            var start = Date.now();
            var data = await request({
                uri: UPSTREAM,
                timeout: 7000
            });
            sb.multi_set(TICKER_CACHE, data, TICKER_TIMER, Date.now());

            log(`fetch ${UPSTREAM} done, ${Date.now() - start} ms`);

            cache[TICKER_CACHE] = data;
            cache[TICKER_TIMER] = Date.now();

            return data;
        }

        var cache = _.zipObject(_.chunk(await sb.multi_get(TICKER_CACHE, TICKER_TIMER), 2));

        if (!cacheExists()) {
            log('[cache miss] ticker cache not exists');
            try {
                await pull();
            } catch (err) {
                res.send(new restify.ServiceUnavailableError('Service Unavailable Now. Please try again later.'));
                return next();
            }
        }

        if (req.params.skipcache || !cacheValid()) {
            log(`[cache miss] ticker cache ${req.params.skipcache ? 'skipped' : 'invalid'}`);
            try {
               await pull();
            } catch (err) {
                log(`fetch error, return stale cache`, err);
            }
        }

        res.setHeader('X-Cache-Generated', Math.floor((cache[TICKER_TIMER] || 0) / 1000));
        res.send(JSON.parse(cache[TICKER_CACHE]));
        next();
    });

    server.get('/timestamp', (req, res, next) => {
        res.send({
            success: true,
            timestamp: ~~(Date.now() / 1000)
        });
        return next();
    });
};