let mysql = require('./mysql');
let _ = require('lodash');

module.exports = {
    txAmountSummary(tx, addr) {
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
        if (inputAddrs.some(el => el.addr.includes(addr))) {       // 支出
            let totalInput = inputAddrs.filter(el => el.addr.includes(addr)).reduce((prev, cur) => {
                return prev + cur.value;
            }, 0);
            let output = outputAddrs.find(el => el.addr.includes(addr));       // 假设只有一个找零
            amount = (output ? output.value : 0) - totalInput;
        } else {        // 收入
            amount = outputAddrs.filter(el => el.addr.includes(addr)).reduce((prev, cur) => prev + cur.value, 0);
        }

        return {
            amount: amount,
            inputs: _(inputAddrs).pluck('addr').flatten().uniq().value(),
            outputs: _(outputAddrs).pluck('addr').flatten().uniq().value()
        };
    }
};