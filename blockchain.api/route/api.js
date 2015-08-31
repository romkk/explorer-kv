var mysql = require('../lib/mysql');
var log = require('debug')('api:route:misc');
var sprintf = require('sprintf').sprintf;
var helper = require('../lib/helper');
var restify = require('restify');
var _ = require('lodash');
var Tx = require('../lib/tx');
var Address = require('../lib/address');
var Block = require('../lib/block');
var validators = require('../lib/custom_validators');
var sb = require('../lib/ssdb')();
var request = require('request-promise');

module.exports = server => {
    server.get('/identify-block/:hash', async (req, res, next) => {
        let rows = await mysql.query('select pool_id, name, key_words, coinbase_address from 0_pool');
        var bag = {};
        rows.forEach(r => {
            if (!bag[r.name]) bag[r.name] = [];
            try {
                bag[r.name] = {
                    id: r.pool_id,
                    re: _.isEmpty(r.key_words) ? /^(\u0000)+$/ : new RegExp(r.key_words, 'i'),  //任何 coinbase 字符串都不可能全部是 0
                    address: r.coinbase_address.split('|')
                };
            } catch (err) {
                log(`[WARN] ${r.name} 正则表达式或地址初始化失败，请检查。`);
            }

        });

        var hash = req.params.hash;

        var blk;
        try {
            blk = await Block.grab(hash, 0, 1, true, !req.params.skipcache);
        } catch (err) {
            return next(new restify.ResourceNotFoundError('Block Not Found'));
        }

        if (!blk.tx.length) {
            return next(new restify.ServiceUnavailableError('Block Not Available'));
        }

        var [tx] = blk.tx;
        var text;
        try {
            text = new Buffer(tx.inputs[0].script, 'hex').toString('utf8');
        } catch (err) {
            return next(err);
        }
        var addr = tx.out[0].addr[0];

        // 查找矿池
        var pool = _.findKey(bag, v => v.re.test(text) || v.address.includes(addr));

        var id = pool ? bag[pool].id : 0;
        var name = pool || 'Unknown';

        if (name != blk.relayed_by) {
            mysql.query(`update 0_blocks set relayed_by = ? where hash = ?`, [id, blk.hash]);
            sb.del(`blk_${blk.hash}`);
        }

        res.send({
            relayedBy: name
        });
        next();
    });

    server.get('/update-pool-definition', async (req, res, next) => {
        const blockchainConf = 'https://raw.githubusercontent.com/blockchain/Blockchain-Known-Pools/master/pools.json';

        let blockchain;
        try {
            blockchain = JSON.parse((await request.get(blockchainConf)).trim());
        } catch (err) {
            res.send({
                success: false
            });
            return next();
        }

        await mysql.query('truncate 0_pool');

        let bag = {};

        // coinbase_tags
        _.each(blockchain.coinbase_tags, ({ name, link }, re) => {
            let o = (bag[name] || (bag[name] = {}));
            o.name = name;
            (o.re || (o.re = [])).push(re);
            o.link = link;
        });

        // payout_addresses
        _.each(blockchain.payout_addresses, ({name, link}, addr) => {
            let o = (bag[name] || (bag[name] = {}));
            o.addr = addr;
            o.name = name;
            o.link = link;
        });

        // insert
        let sql = `insert into 0_pool (name, key_words, coinbase_address, link) values`;
        let params = [];
        _.each(bag, (v, name) => {
            params.push([ name, v.re && v.re.join('|') || '', v.addr || '', v.link || '']);
        });

        mysql.query(sql + params.map(pArr => `(${pArr.map(p => mysql.pool.escape(p)).join(',')})`).join(','));

        res.send({
            success: true,
            bag: bag,
            blockchain: blockchain
        });
        next();
    });
};