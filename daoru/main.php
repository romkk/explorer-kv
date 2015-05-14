<?php
require __DIR__ . '/bootstrap.php';

//demo
$bitcoinClient = Bitcoin::make();

// 1. clear temp log
Txlogs::clearTempLogs(Txlogs::getTempLogs());
// 2. compare block height and block hash of local and remote
$queue = BlockQueue::make();

//while (true) {
    $latestRemoteBlockInfo = $bitcoinClient->bm_get_best_block();
    $remote = Block::createFromBlockDetail($bitcoinClient->getBlockByHeight($latestRemoteBlockInfo['height']));

    if ($queue->diff($remote)) {
        $latestBlock = $queue->getBlock();

        if (is_null($latestBlock)) {
            $detail = $bitcoinClient->getBlockByHeight(0);
            $detail['previousblockhash'] = '';
        } else {
            $detail = $bitcoinClient->getBlockByHeight($latestBlock->getHeight() + 1);
        }
        $block = Block::createFromBlockDetail($detail);
        list($newBlock, $orphanBlocks) = $queue->digest($block);
        // TODO: rollback orphan blocks
        assert(count($orphanBlocks) === 0);
        $newBlock->insert();
    }
//}