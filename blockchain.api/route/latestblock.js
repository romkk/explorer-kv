var mysql = require('../lib/mysql');
var Block = require('../lib/block');
var log = require('debug')('api:route:latestblock');

module.exports = (server) => {
    server.get('/latestblock/', async (req, res, next)=> {
        var latestHeight = await Block.getLatestHeight();
        var [blk] = await Block.grabByHeight(latestHeight, !req.params.skipcache);

        res.send({
            height: blk.height,
            hash: blk.hash,
            time: blk.time,
            txIndexes: blk.tx
        });

        next();
    });
};
