var restify = require('restify');
var mysql = require('../lib/mysql');
var sprintf = require('sprintf').sprintf;
var log = require('debug')('api:cli');

var app = restify.createServer();

/**
 {
	"hash":"0000000000000538200a48202ca6340e983646ca088c7618ae82d68e0c76ef5a",
	"time":1325794737,
	"block_index":841841,
	"height":160778,
	"txIndexes":[13950369,13950510,13951472]
 }
 */

app.get('/latestblock/', (req, res, next)=> {
    mysql.query('select id, block_hash, block_height, chain_id, hex, created_at from 0_raw_blocks where chain_id = 0 order by id desc limit 1', (err, rows)=> {
        if (err) throw err;

        var ret = {};
        var block = rows[0];

        log('获取 block', block);

        ret.hash = block.block_hash;
        ret.time = block.time;
        ret.height = block.block_height;
        ret.block_index = block.id;

        var table = sprintf('block_txs_%04d', parseInt(block.id, 10) % 64);
        var sql = `select tx_id from ${table} where block_id = ?`;
        log(sql);
        mysql.query(sql, [block.id], (err, rows)=>{
            if (err) throw err;

            log('获取 txs', rows);

            ret.txIndexes = rows.map((r) => r.tx_id);

            res.end(JSON.stringify(ret));
            next();
        });
    });

});

app.listen(3000, ()=> {
    console.log('listen on 3000');
});
