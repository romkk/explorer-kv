let restify = require('restify');
let validate = require('../lib/valid_json');
let log = require('debug')('wallet:route:tx');
let _ = require('lodash');
let blockData = require('../lib/block_data');
let request = require('request-promise');
let config = require('config');
let bitcoind = require('../lib/bitcoind');
let moment = require('moment');
let helper = require('../lib/helper');
let assert = require('assert');
let txnote = require('../lib/txnote');
let mysql = require('../lib/mysql');
let m = require('mysql');
let Transaction = require('bitcore').Transaction;

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
    server.post('/tx/compose', validate('tx'), async (req, res, next) => {
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

    server.post('/tx', validate('txPublish'), async (req, res, next) => {
        let { hex, note } = req.body;

        let hash;
        try {
            hash = Transaction(hex).hash;
        } catch (err) {
            res.send({
                success: false,
                code: 'TxPublishInvalidHex',
                message: 'invalid hex'
            });
            return next();
        }

        try {
            let txHash;
            await mysql.transaction(async conn => {


                let sql = `select wid, txhash, note from tx_note where txhash = ? lock in share mode`;
                if (await conn.selectOne(sql, [hash])) {
                    let e = new Error();
                    e.code = 'TxPublishDuplicateTx';
                    e.message = 'this transaction has been published';
                    throw e;
                }

                log(`尝试发送交易 hex = ${hex.slice(0, 50)}, note = ${note}`);
                try {
                    txHash = await bitcoind('sendrawtransaction', hex);
                } catch (err) {
                    log(`发送交易失败 hex = ${hex.slice(0, 50)}, note = ${note}`);
                    throw err;
                }

                // 保存
                sql = `insert into tx_note
                   (wid, txhash, note, created_at, updated_at)
                   values
                   (?, ?, ?, now(), now())`;
                await conn.query(sql, [req.token.wid, txHash, note]);
            });

            res.send({
                success: true,
                txhash: txHash,
                note: note
            });
        } catch (err) {
            console.log(err.stack);
            if (err.code == 'TxPublishDuplicateTx') {
                res.send({
                    success: false,
                    code: err.code,
                    message: err.message
                });
            } else if (err.name == 'StatusCodeError') {
                res.send({
                    success: false,
                    description: _.get(err, 'response.body.error.message', null),
                    message: 'Publish failed',
                    code: 'TxPublishBitcoindError'
                });
            } else {
                res.send(new restify.InternalServerError('Internal Error'));
            }
        }
        next();
    });

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

        let sql = `select txhash, note from tx_note where txhash in (${ result.map(r => '?').join(', ') })`;
        let noteMap = {};
        (await mysql.query(sql, result.map(r => r.hash))).forEach(r => noteMap[r.txhash] = r.note);

        let addrs = req.params.active.split('|');
        let ret = [];
        for (let r of result) {
            ret.push(_.extend(helper.txAmountSummary(r, addrs), {
                confirmations: r.confirmations,
                txhash: r.hash,
                note: noteMap[r.hash] || '',
                timestamp: r.time
            }));
        }

        res.send(ret);
        return next();
    });

    server.get('/tx/:txhash', async (req, res, next) => {
        req.checkParams('txhash', 'should be a valid txhash').isLength(64, 64);
        req.sanitize('txhash').toString();

        req.checkQuery('active', 'should be a \'|\' separated address list').matches(/^([a-zA-Z0-9]{33,35})(\|[a-zA-Z0-9]{33,35})*$/);
        req.sanitize('active').toString();

        var errors = req.validationErrors();

        if (errors) {
            return next(new restify.InvalidArgumentError({
                message: errors
            }));
        }

        let tx, latestBlock, note, addrs = req.params.active.split('|');

        let sql = `select note from tx_note where txhash = ? and wid = ?`;
        try {
            [tx, latestBlock, note] = await* [
                blockData(`/rawtx/${req.params.txhash}`),
                blockData('/latestblock'),
                mysql.pluck(sql, 'note', [req.params.txhash, req.token.wid])
            ];
        } catch (err) {
            res.send(new restify.InternalServerError('Internal Error'));
            return next();
        }

        res.send(_.extend(helper.txAmountSummary(tx, addrs), {
            confirmations: latestBlock.height == -1 ? 0 : latestBlock.height - tx.block_height + 1,
            txhash: tx.hash,
            note: note || '',
            timestamp: tx.time
        }));
        return next();
    });
};
