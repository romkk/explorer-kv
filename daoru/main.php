<?php
require __DIR__ . '/bootstrap.php';

Log::info('程序启动');

//demo
$bitcoinClient = Bitcoin::make();

// 1. clear temp log
Log::info('初始化：开始清理临时记录');
Txlogs::clearTempLogs(Txlogs::getTempLogs());
Log::info('初始化：临时记录清理完毕');

// 2. compare block height and block hash of local and remote
Log::info('初始化：开始生成块队列');
$queue = BlockQueue::make();
Log::info('初始化：生成块队列完成');

//while (true) {
    Log::info('获取当前最新块信息');
    $latestRemoteBlockInfo = $bitcoinClient->bm_get_best_block();
    Log::info('当前最新块信息', $latestRemoteBlockInfo);
    $remote = Block::createFromBlockDetail($bitcoinClient->getBlockByHeight($latestRemoteBlockInfo['height']));

    if ($queue->diff($remote)) {
        $latestBlock = $queue->getBlock();
        Log::info('检测到当前块与最新块不一致，开始更新本地块信息', [
            'local' => is_null($latestBlock) ? null : $latestBlock->toArray(),
            'remote' => $remote->toArray(),
        ]);

        if (is_null($latestBlock)) {
            $detail = $bitcoinClient->getBlockByHeight(0);
            $detail['previousblockhash'] = '';
            Log::info('初始化创世纪块', $detail);
        } else {
            $detail = $bitcoinClient->getBlockByHeight($latestBlock->getHeight() + 1);
            Log::info(sprintf('当前高度 %d，获取下一个块高度 %d',$latestBlock->getHeight(), $detail['height']));
        }
        $block = Block::createFromBlockDetail($detail);
        list($newBlock, $orphanBlocks) = $queue->digest($block);
        Log::info(sprintf('digest 完成，孤块共计 %d 个', count($orphanBlocks)), [
            'newBlock' => $newBlock->toArray(),
            'orphanBlocks' => $orphanBlocks->map(function(Block $block) {
                return $block->toArray();
            }),
        ]);
        // TODO: rollback orphan blocks
        assert(count($orphanBlocks) === 0);
        $newBlock->insert();
    }
//}