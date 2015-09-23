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
let api = require('../lib/api');

module.exports = server => {
    server.get('/identify-block-batch/:start/:end', async (req, res, next) => {
        var start = Number(req.params.start),
            end = Number(req.params.end);

        for (; start <= end; start++) {
            res.write(`block_id = ${start}\n`);
            await api.identifyBlock(start, !req.params.skipcache);
        }

        res.end('done\n');
        next();
    });

    server.get('/identify-block/:hash', async (req, res, next) => {
        var hash = req.params.hash;
        var info = await api.identifyBlock(hash, !req.params.skipcache);

        if (info instanceof Error) {
            res.send(info);
            return next();
        }

        if (info.name != info.blk.relayed_by) {
            mysql.query(`update 0_blocks set relayed_by = ? where hash = ?`, [id, info.blk.hash]);
            sb.del(`blk_${info.blk.hash}`);
        }

        res.send({ relayedBy: info.name});
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