var mysql = require('../lib/mysql');
var Address = require('../lib/address');
var log = require('debug')('api:route:address');
var bitcore = require('bitcore');
var helper = require('../lib/helper');
var restify = require('restify');
var _ = require('lodash');
var moment = require('moment');
var validators = require('../lib/custom_validators');
var AddressTxList = require('../lib/AddressTxList');
var Tx = require('../lib/tx');
var PriorityQueue = require('js-priority-queue');
var Block = require('../lib/block');

module.exports = (server) => {
    server.get('/address/:addr', async (req, res, next)=> {
        req.checkParams('addr', 'Not a valid address').isValidAddress();

        req.checkQuery('offset', 'should be a valid number').optional().isNumeric();
        req.sanitize('offset').toInt();

        req.checkQuery('timestamp', 'should be a valid timestamp').optional().isNumeric({ min: 0, max: moment.utc().unix() + 3600 });    // +3600 以消除误差
        req.sanitize('timestamp').toInt();

        req.checkQuery('limit', 'should be between 1 and 50').optional().isNumeric().isInt({ max: 50, min: 1});
        req.sanitize('limit').toInt();

        req.checkQuery('sort', 'should be desc or asc').optional().isIn(['desc', 'asc']);

        var errors = req.validationErrors();

        if (errors) {
            return next(new restify.InvalidArgumentError({
                message: errors
            }));
        }

        var addr = await Address.grab(req.params.addr, !req.params.skipcache);
        if (addr == null) {
            return new restify.ResourceNotFoundError('Address not found');
        }

        var atList = new AddressTxList(addr.attrs, req.params.timestamp, req.params.sort);
        atList = await atList.slice(req.params.offset, req.params.limit);
        addr.txs = await Tx.multiGrab(atList.map(el => el.tx_id), !req.params.skipcache);

        res.send(addr);
        next();
    });

    server.get('/address-tx/', async (req, res, next) => {
        req.checkQuery('timestamp', 'should be a valid timestamp').optional().isNumeric({ min: 0, max: moment.utc().unix() + 3600 });    // +3600 以消除误差
        req.sanitize('timestamp').toInt();

        req.checkQuery('limit', 'should be between 1 and 50').optional().isNumeric().isInt({ max: 50, min: 1});
        req.sanitize('limit').toInt();

        req.checkQuery('sort', 'should be desc or asc').optional().isIn(['desc', 'asc']);

        var errors = req.validationErrors();

        if (errors) {
            return next(new restify.InvalidArgumentError({
                message: errors
            }));
        }

        var err = validators.isValidAddressList(req.query.active);
        if (err != null) {
            return next(err);
        }

        var limit = req.params.limit || 50;
        var sort = req.params.sort || 'desc';
        var parts = req.params.active.trim().split('|');
        var addrs = _.compact(await Address.multiGrab(parts, !req.params.skipcache));
        var its = await* addrs.map(addr => {
            var atList = new AddressTxList(addr.attrs, req.params.timestamp, sort);
            return atList.iter();
        });
        var pq = new PriorityQueue({
            comparator: sort == 'desc' ? (a, b) => b[0].tx_height - a[0].tx_height : (a, b) => a[0].tx_height - b[0].tx_height
        });

        async function push(i) {
            var it = its[i];
            if (it == false) return;

            var c = it.next();
            if (it.done) {
                its[i] = false;
                return;
            }
            try {
                var v = await c.value;
                pq.queue([v, i]);
            } catch (err) {
                its[i] = false;
            }
        }

        // 初始化 Priority Queue
        await* its.map((it, i) => {
            return push(i);
        });

        var ret = [];

        while (limit--) {
            try {
                let [v, i] = pq.dequeue();
                ret.push(v.tx_id);
                await push(i);
            } catch (err) {
                break;  //no more element
            }
        }

        var [h, txs] = await* [Block.getLatestHeight(), Tx.multiGrab(ret, !req.params.skipcache)];
        res.send(_.compact(txs).map(tx => {
            tx.confirmations = tx.block_height == -1 ? 0 : h - tx.block_height + 1;
            return tx;
        }));
        next();
    });

    server.get('/multiaddr', async (req, res, next) => {
        var err = validators.isValidAddressList(req.query.active);
        if (err != null) {
            return next(err);
        }

        var parts = req.params.active.trim().split('|');

        var ret = await Address.multiGrab(parts, !req.params.skipcache);

        ret = ret.map(addr => {
            if (addr == null) {
                return null;
            }

            var a = addr.attrs;

            return {
                final_balance: a.total_received - a.total_sent,
                address: a.address,
                total_sent: a.total_sent,
                total_received: a.total_received,
                n_tx: a.tx_count,
                hash160: helper.addressToHash160(a.address)
            };
        });

        res.send({
            addresses: ret
        });
        next();
    });
};