var restify = require('restify');
var validate = require('../lib/valid_json');
var log = require('debug')('wallet:route:tx');
var _ = require('lodash');
var blockData = require('../lib/block_data');
var request = require('request-promise');
var config = require('config');
var bitcoind = require('../lib/bitcoind');
var moment = require('moment');
var helper = require('../lib/helper');
var assert = require('assert');
var txnote = require('../lib/txnote');

// 获取 unspent，找出合适的 unspent
async function getUnspentTxs(sentFrom, amount, offset) {
    const limit = 200;
    var aggregated = 0;
    var aggregatedTxs = [];

    while (aggregated <= amount) {
        let unspentList = await blockData('/unspent', {     // if error, throw it
            active: sentFrom.join('|'),
            offset: offset,
            limit: limit
        });

        if (!unspentList.unspent_outputs.length) break;

        unspentList.unspent_outputs.every(tx => {
            aggregated += tx.value;
            aggregatedTxs.push(tx);
            offset++;
            //log(`tx.value = ${tx.value}, offset = ${offset}`);
            return aggregated <= amount;
        });
    }

    return [offset, aggregated, aggregatedTxs];
}

// http://bitcoin.stackexchange.com/questions/1195/how-to-calculate-transaction-size-before-sending
function estimateFee(txSize, amountAvailable, feePerKB) {
    var estimatedFee = Math.ceil(txSize / 1000) * feePerKB;

    if (estimatedFee < amountAvailable) {
        txSize += 20 + 4 + 34 + 4;                // Safe upper bound for change address script size in bytes
        estimatedFee = Math.ceil(txSize / 1000) * feePerKB;
    }

    return estimatedFee;
}

module.exports = server => {
    server.get('/tx', async (req, res, next) => {
        req.checkQuery('active', 'should be a \'|\' separated address list').matches(/^([a-zA-Z0-9]{33,35})(\|[a-zA-Z0-9]{33,35})*$/);
        req.sanitize('active').toString();

        req.checkQuery('timestamp', 'should be a valid timestamp').optional().isNumeric({ min: 0, max: moment.utc().unix() + 3600 });    // +3600 以消除误差
        req.sanitize('timestamp').toInt();

        req.checkQuery('offset', 'should be a valid number').optional().isNumeric().isInt({ min: 0});
        req.sanitize('offset').toInt();

        req.checkQuery('limit', 'should be between 1 and 50').optional().isNumeric().isInt({ max: 50, min: 1});
        req.sanitize('limit').toInt();

        req.checkQuery('sort', 'should be desc or asc').optional().isIn(['desc', 'asc']);

        var errors = req.validationErrors();

        if (errors) {
            return next(new restify.InvalidArgumentError({
                message: errors
            }));
        }

        let params = _.pick(req.params, ['timestamp', 'offset', 'limit', 'sort', 'active']);

        let result;
        try {
            result = await blockData(`/address-tx`, params);
        } catch (err) {
            res.send(new restify.InternalServerError('Internal Error'));
            return next();
        }

        let addrs = req.params.active.split('|');
        let ret = [];
        for (let r of result) {
            ret.push(_.extend(helper.txAmountSummary(r, addrs), {
                confirmations: r.confirmations,
                txhash: r.hash,
                timestamp: r.time
            }));
        }

        res.send(ret);
        return next();
    });

    server.post('/tx', validate('tx'), async (req, res, next) => {
        var feePerKB = req.body.fee_per_kb,
            sentFrom = req.body.from,
            sentTo = req.body.to;

        var totalSentAmount = _.sum(sentTo, 'amount');

        // 检查余额是否足够
        var totalUnspentAmount;
        try {
            let apiResponse = await blockData('/multiaddr', {
                active: sentFrom.join('|')
            });
            totalUnspentAmount = _.sum(apiResponse.addresses, 'final_balance');
        } catch (err) {
            return next(err);
        }

        if (totalUnspentAmount <= totalSentAmount) {    //等于时则不足以支付手续费
            res.send({
                success: false,
                code: 'TxUnaffordable',
                message: `totalSentAmount = ${totalSentAmount}, you got = ${totalUnspentAmount}, diff = ${totalUnspentAmount - totalSentAmount}`
            });
            return next();
        }

        let offset = 0, aggregated = 0, aggregatedTxs = [], txSize = 0, fee = 0;
        let iter = 1;
        do {
            // 获得 unspent 列表
            let curOffset, curAggregated, curAggregatedTxs;
            try {
                [curOffset, curAggregated, curAggregatedTxs] = await getUnspentTxs(sentFrom, fee + totalSentAmount - aggregated, offset);
            } catch (err) {
                return next(err);
            }

            if (curAggregated == 0) {  //没有更多了
                // balance change ?
                try {
                    let apiResponse = await blockData('/multiaddr', {
                        active: sentFrom.join('|')
                    });
                    let currentAmount = _.sum(apiResponse.addresses, 'final_balance');
                    if (totalUnspentAmount != currentAmount) {      //余额变动，重新启动
                        log(`检测到 unspent 余额变动, previous = ${totalUnspentAmount}, currentAmount = ${currentAmount}`);
                        totalUnspentAmount = currentAmount;
                        offset = aggregated = aggregatedTxs = txSize = fee = 0;
                        continue;
                    }
                } catch (err) {
                    //do nothing
                }

                res.send({
                    success: false,
                    code: 'TxUnaffordable',
                    message: `estimated fee = ${fee}, total spent = ${totalSentAmount}, you got = ${totalUnspentAmount}, will send = ${aggregated}, diff = ${totalUnspentAmount - totalSentAmount - fee}`
                });
                return next();
            }

            offset = curOffset;
            aggregated += curAggregated;
            aggregatedTxs.push.apply(aggregatedTxs, curAggregatedTxs);
            // 计算手续费
            txSize = 148 * aggregatedTxs.length       // input
                + 34 * sentTo.length               // output
                + 10;                              // fixed bytes
            fee = estimateFee(txSize, aggregated, feePerKB);
            log(`iter = ${iter++}, offset = ${offset}, fee = ${fee}, totalSentAmount = ${totalSentAmount}, aggregated = ${aggregated}, aggregatedTxs.length = ${aggregatedTxs.length}`);
        } while (fee + totalSentAmount > aggregated);

        res.send({
            success: true,
            fee: fee,
            unspent_txs: aggregatedTxs
        });
        next();
    });

    server.get('/tx/note', async (req, res, next) => {
        req.checkQuery('txhash', 'should be a valid hash').isLength(64, 64);
        req.sanitize('txhash').toString();

        var errors = req.validationErrors();

        if (errors) {
            return next(new restify.InvalidArgumentError({
                message: errors
            }));
        }

        let txhash = req.params.txhash;
        let note = await txnote.getNote(req.token.wid, txhash);
        if (note == null) {
            res.send({
                success: false,
                code: 'TxNoteNotFound',
                message: 'tx note has not been created'
            });
        } else {
            res.send({
                success: true,
                note: note,
                txhash: txhash
            });
        }
        next();
    });

    server.post('/tx/note', validate('createTxNote'), async (req, res, next) => {
        let {txhash, note} = req.body;
        try {
            await txnote.setNote(req.token.wid, txhash, note);
        } catch (err) {
            if (err.code && err.message) {
                res.send(_.extend({success: false}, _.pick(err, ['code', 'message'])));
            } else {
                res.send(new restify.InternalServerError('Internal Error'));
            }
            return next();
        }
        res.send({
            success: true
        });
        return next();
    });
};
