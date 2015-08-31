let mysql = require('../lib/mysql');
let Address = require('../lib/address');
let log = require('debug')('api:route:address');
let bitcore = require('bitcore');
let helper = require('../lib/helper');
let restify = require('restify');
let _ = require('lodash');
let moment = require('moment');
let validators = require('../lib/custom_validators');
let AddressTxList = require('../lib/AddressTxList');
let Tx = require('../lib/tx');
let PriorityQueue = require('js-priority-queue');
let Block = require('../lib/block');

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

        let errors = req.validationErrors();

        if (errors) {
            return next(new restify.InvalidArgumentError({
                message: errors
            }));
        }

        let addr = await Address.grab(req.params.addr, !req.params.skipcache);
        if (addr == null) {
            res.send(new restify.ResourceNotFoundError('Address not found'));
            return next();
        }

        let atList = new AddressTxList(addr.attrs, req.params.timestamp, req.params.sort);
        atList = await atList.slice(req.params.offset, req.params.limit);
        addr.txs = await Tx.multiGrab(atList.map(el => el.tx_id), !req.params.skipcache);

        res.send(addr);
        next();
    });

    server.get('/address-tx/', async (req, res, next) => {
        req.checkQuery('active', 'should be a \'|\' separated address list').matches(/^([a-zA-Z0-9]{33,35})(\|[a-zA-Z0-9]{33,35})*$/);
        req.sanitize('active').toString();

        req.checkQuery('timestamp', 'should be a valid timestamp').optional().isNumeric({ min: 0, max: moment.utc().unix() + 3600 });    // +3600 以消除误差
        req.sanitize('timestamp').toInt();

        req.checkQuery('offset', 'should be a valid number').optional().isNumeric().isInt({ min: 0});
        req.sanitize('offset').toInt();

        req.checkQuery('limit', 'should be between 1 and 50').optional().isNumeric().isInt({ max: 50, min: 1});
        req.sanitize('limit').toInt();

        req.checkQuery('sort', 'should be desc or asc').optional().isIn(['desc', 'asc']);

        let errors = req.validationErrors();

        if (errors) {
            return next(new restify.InvalidArgumentError({
                message: errors
            }));
        }

        let err = validators.isValidAddressList(req.query.active);
        if (err != null) {
            return next(err);
        }

        let offset = req.params.offset || 0;
        let limit = req.params.limit || 50;
        let sort = req.params.sort || 'desc';
        let parts = req.params.active.trim().split('|');
        let addrs = _.compact(await Address.multiGrab(parts, !req.params.skipcache));

        let its = await* addrs.map(addr => {
            let atList = new AddressTxList(addr.attrs, req.params.timestamp, sort);
            return atList.iter();
        });
        let pq = new PriorityQueue({
            comparator: sort == 'desc' ? (a, b) => {
                let ret;
                if (a[0].tx_height == -1 && b[0].tx_height != -1) {
                    ret = -1;
                } else if (a[0].tx_height != -1 && b[0].tx_height == -1) {
                    ret = 1;
                } else {
                    ret = b[0].tx_height - a[0].tx_height;
                }

                if (ret == 0) {
                    ret = b[0].idx - a[0].idx;
                }

                return ret;
            } : (a, b) => {
                let ret;
                if (a[0].tx_height == -1 && b[0].tx_height != -1) {
                    ret = 1;
                } else if (a[0].tx_height != -1 && b[0].tx_height == -1) {
                    ret = -1;
                } else {
                    ret = a[0].tx_height - b[0].tx_height;
                }

                if (ret == 0) {
                    ret = a[0].idx - b[0].idx;
                }

                return ret;
            }
        });

        async function queue(i) {
            let it = its[i];
            if (it == false) return;

            let v = await it();
            if (v == null) {
                return its[i] = false;
            }

            pq.queue([v, i]);
        }

        // 初始化 Priority Queue
        await* its.map((it, i) => queue(i));

        let txIdList = [];
        let txInList = {};

        while ((offset--) > 0 || limit--) {
            let v, i;
            try {
                [v, i] = pq.dequeue();
            } catch (err) {
                break;  //no more element
            }

            await queue(i);
            if (offset < 0 && !_.has(txInList, v.tx_id)) {
                txIdList.push(v.tx_id);
                txInList[v.tx_id] = 1;
            }
        }

        let [h, txs] = await* [Block.getLatestHeight(), Tx.multiGrab(txIdList, !req.params.skipcache)];
        res.send(_.compact(txs).map(tx => {
            tx.confirmations = tx.block_height == -1 ? 0 : h - tx.block_height + 1;
            return tx;
        }));
        next();
    });

    server.get('/multiaddr', async (req, res, next) => {
        let err = validators.isValidAddressList(req.params.active);
        if (err != null) {
            res.send(err);
            return next();
        }

        let parts = req.params.active.trim().split('|');

        let ret = await Address.multiGrab(parts, !req.params.skipcache);

        ret = ret.map(addr => {
            if (addr == null) {
                return null;
            }

            let a = addr.attrs;

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