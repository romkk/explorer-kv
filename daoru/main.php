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
Log::info(sprintf('初始化：生成块队列完成，长度为 %d', $queue->length()));

// 3. loop
while (true) {
    Log::info('获取当前最新块信息');
    $latestRemoteBlockInfo = $bitcoinClient->bm_get_best_block();
    Log::info('当前最新块信息', $latestRemoteBlockInfo);
    $remote = Block::createFromBlockDetail($bitcoinClient->bm_get_block_detail($latestRemoteBlockInfo['hash']));

    if ($queue->diff($remote)) {

        $latestBlock = $queue->getBlock();
        Log::info('检测到当前块与最新块不一致，开始更新本地块信息', [
            'local' => is_null($latestBlock) ? null : $latestBlock->toArray(),
            'remote' => $remote->toArray(),
        ]);

        $needBackof = !is_null($latestBlock) && $latestBlock->getHeight() === $remote->getHeight();

        if (is_null($latestBlock)) {        //创世纪块
            $detail = $bitcoinClient->getBlockByHeight(0);
            $detail['previousblockhash'] = '';
            Log::info('初始化创世纪块', $detail);
        } else if ($needBackof) {    // 高度相同，hash 不同
            Log::notice('高度相同，但是 hash 不同', [
                'height' => $latestBlock->getHeight(),
                'localHash' => $latestBlock->getHash(),
                'remoteHash' => $remote->getHash(),
            ]);
            $detail = $bitcoinClient->bm_get_block_detail($latestBlock->getHash());      // 获取同高度 block
            Log::info(sprintf('当前高度 %d，获取下一个块高度 %d',$latestBlock->getHeight(), $detail['height']));
        } else {
            //TODO 修改为 bm_get_block_detail
            $detail = $bitcoinClient->getBlockByHeight($latestBlock->getHeight() + 1);
            Log::info(sprintf('当前高度 %d，获取下一个块高度 %d',$latestBlock->getHeight(), $detail['height']));
        }

        $block = Block::createFromBlockDetail($detail);
        $queue->digest($block, $newBlock, $orphanBlocks, intval($needBackof));
        Log::info(sprintf('digest 完成，孤块共计 %d 个', count($orphanBlocks)), [
            'newBlock' => $newBlock->toArray(),
            'orphanBlocks' => $orphanBlocks->map(function(Block $block) {
                return $block->toArray();
            }),
        ]);

        if (count($orphanBlocks)) {
            $queue->rollback($orphanBlocks);
        }

        $newBlock->insert();

    } else {
        sleep(10);  //如果一致，则睡眠 10s 再继续轮询
    }
}