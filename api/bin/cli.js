var restify = require('restify');
var mysql = require('../lib/mysql');
var sprintf = require('sprintf').sprintf;
var log = require('debug')('api:server');

var server = restify.createServer();

server.get('/', function(req, res, next) {
    res.end('hello world');
});


server.use(restify.acceptParser(server.acceptable));
server.use(restify.authorizationParser());
server.use(restify.dateParser());
server.use(restify.queryParser());
server.use(restify.urlEncodedBodyParser());

/**
 {
	"hash":"0000000000000538200a48202ca6340e983646ca088c7618ae82d68e0c76ef5a",
	"time":1325794737,
	"block_index":841841,
	"height":160778,
	"txIndexes":[13950369,13950510,13951472]
 }
 */

server.get('/latestblock/', (req, res, next)=> {
    var ret = {};

    mysql.selectOne('select id, block_hash, block_height, chain_id, hex, created_at from 0_raw_blocks where chain_id = 0 order by id desc limit 1')
        .then(block => {
            ret.hash = block.block_hash;
            ret.time = block.time;
            ret.height = block.block_height;
            ret.block_index = block.id;

            var table = sprintf('block_txs_%04d', parseInt(block.id, 10) % 64);
            var sql = `select tx_id from ${table} where block_id = ?`;

            return mysql.list(sql, 'tx_id', [block.id]);
        }).then(txIndexes => {
            ret.txIndexes = txIndexes;
            res.end(JSON.stringify(ret));
            next();
        });
});

server.listen(3000, ()=> {
    log('listen on 3000');
});