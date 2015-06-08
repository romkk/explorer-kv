var mysql = require('../lib/mysql');
var Block = require('../lib/block');
var log = require('debug')('api:route:latestblock');

module.exports = (server) => {
    server.get('/latestblock/', (req, res, next)=> {
        Block.getLatestHeight()
            .then(height => Block.grab(height, 0, 0, false, !req.params.skipcache))
            .then(blk => {
                res.send({
                    height: blk.height,
                    hash: blk.hash,
                    time: blk.time,
                    txIndexes: blk.tx
                });
                next();
            });
    });
};
