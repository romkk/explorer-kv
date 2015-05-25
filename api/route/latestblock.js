var mysql = require('../lib/mysql');
var Block = require('../lib/block');
var log = require('debug')('api:route:latestblock');

/**
 * Get latest block info.
 *
 * @URL /latestblock
 *
 * {
 *	"hash":"0000000000000538200a48202ca6340e983646ca088c7618ae82d68e0c76ef5a",
 *	"time":1325794737,
 *	"block_index":841841,
 * 	"height":160778,
 * 	"txIndexes":[13950369,13950510,13951472]
 * }
 */

module.exports = (server) => {
    server.get('/latestblock/', (req, res, next)=> {
        var ret = {};

        var sql = `select id, block_hash, block_height, chain_id, hex, created_at
                   from 0_raw_blocks
                   where chain_id = 0 order by id desc limit 1`;
        mysql.selectOne(sql)
            .then(block => {
                ret.hash = block.block_hash;
                ret.time = block.time;
                ret.height = block.block_height;
                ret.block_index = block.id;

                var table = Block.getBlockTxTableByBlockId(block.id);
                var sql = `select tx_id from ${table} where block_id = ? order by position asc`;

                return mysql.list(sql, 'tx_id', [block.id]);
            }).then(txIndexes => {
                ret.txIndexes = txIndexes;
                res.send(JSON.stringify(ret));
                next();
            });
    });
};
