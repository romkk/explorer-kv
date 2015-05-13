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
    $detail = $bitcoinClient->getBlockByHeight($latestRemoteBlockInfo['height']);
    $remote = new Block($detail['hash'], $detail['previousblockhash'], $detail['height'], $detail['time']);
    $remote->setHex($detail['rawhex']);
    $remote->setTxs(array_map(function($tx) use ($remote) {
        $t = new Tx($remote, $tx['hash']);
        $t->setHex($tx['rawhex']);
        return $t;
    }, $detail['tx']));

    if ($queue->diff($remote)) {
        $latestBlock = $queue->getBlock();

        if (is_null($latestBlock)) {
            $detail = $bitcoinClient->getBlockByHeight(0);
            $detail['previousblockhash'] = '';
        } else {
            $detail = $bitcoinClient->getBlockByHeight($latestBlock->getHeight() + 1);
        }
        $block = new Block($detail['hash'], $detail['previousblockhash'], $detail['height'], $detail['time']);
        $block->setHex($detail['rawhex']);
        $block->setTxs(array_map(function($tx) use ($block) {
            $t = new Tx($block, $tx['hash']);
            $t->setHex($tx['rawhex']);
            return $t;
        }, $detail['tx']));
        list($newBlock, $orphanBlocks) = $queue->digest($block);
        // TODO: rollback orphan blocks
        assert(count($orphanBlocks) === 0);
        $newBlock->insert();
    }
//}