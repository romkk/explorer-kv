var sprintf = require('sprintf').sprintf;

module.exports = {
    getBlockTxTableByBlockId(blockId) {
        return sprintf('block_txs_%04d', parseInt(blockId, 10) % 64);
    }
};