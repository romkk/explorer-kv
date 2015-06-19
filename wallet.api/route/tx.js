var restify = require('restify');
var validate = require('../lib/valid_json');
var log = require('debug')('wallet:route:tx');
var _ = require('lodash');
var blockData = require('../lib/block_data');

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
function estimateFee(txSize, amountAvaiable, feePerKB) {
    var estimatedFee = Math.ceil(txSize / 1000) * feePerKB;

    if (estimatedFee < amountAvaiable) {
        txSize += 20 + 4 + 34 + 4;                // Safe upper bound for change address script size in bytes
        estimatedFee = Math.ceil(txSize / 1000) * feePerKB;
    }

    return estimatedFee;
}

module.exports = server => {
    server.post('/tx', validate('tx'), async (req, res, next) => {
        var feePerKB = req.body.fee_per_kb,
            sentFrom = req.body.from,
            sentTo = req.body.to;

        var totalSentAmount = _.sum(sentTo, 'amount');

        // 检查余额是否足够
        var totalUnspentAmount;
        try {
            totalUnspentAmount = _.sum(await blockData('/multiaddr', {
                active: sentFrom.join('|')
            }), 'final_balance');
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
                    let currentAmount = _.sum(await blockData('/multiaddr', {
                        active: sentFrom.join('|')
                    }), 'final_balance');
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