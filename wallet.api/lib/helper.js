let mysql = require('./mysql');
let _ = require('lodash');

module.exports = {
    txAmountSummary(tx, addrs) {
        let inputAddrs = [], outputAddrs = [];
        if (!tx.inputs[0].prev_out) {   // coinbase 交易
            outputAddrs.push({
                value: tx.out[0].value,
                addr: tx.out[0].addr
            });
        } else {    // 普通交易
            inputAddrs = tx.inputs.map(el => ({
                value: el.prev_out.value,
                addr: el.prev_out.addr
            }));
            outputAddrs = tx.out.map(el => ({
                value: el.value,
                addr: el.addr
            }));
        }

        // 计算 amount
        let amount = 0;
        if (inputAddrs.some(el => el.addr.some(a => addrs.includes(a)))) {       // 支出
            let totalInput = inputAddrs.filter(el => el.addr.some(a => addrs.includes(a))).reduce((prev, cur) => prev + cur.value, 0);
            let totalOutput = outputAddrs.filter(el => el.addr.some(a => addrs.includes(a))).reduce((prev, cur) => prev + cur.value, 0);
            amount = totalOutput - totalInput;
        } else {        // 收入
            amount = outputAddrs.filter(el => el.addr.some(a => addrs.includes(a))).reduce((prev, cur) => prev + cur.value, 0);
        }

        return {
            amount: amount,
            inputs: _(inputAddrs).pluck('addr').flatten().uniq().value(),
            outputs: _(outputAddrs).pluck('addr').flatten().uniq().value()
        };
    }
};