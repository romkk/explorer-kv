<?php
use Illuminate\Support\Collection;

require __DIR__ . '/bootstrap.php';

Log::info('程序启动');

//demo
$bitcoinClient = Bitcoin::make();
$pool = new RawMemPool();

// 1. clear temp log
Log::info('初始化：开始清理临时记录');
$pool->rollback();
Log::info('初始化：临时记录清理完毕');

// 2. compare block height and block hash of local and remote
Log::info('初始化：开始生成块队列');
$queue = BlockQueue::make();
Log::info(sprintf('初始化：生成块队列完成，长度为 %d', $queue->length()));

// 3. loop
while (true) {
    reportStatus('MAIN.PHP');

    Log::info('获取当前最新块信息');
    $latestRemoteBlockInfo = $bitcoinClient->bm_get_best_block();
    Log::info('当前最新块信息', $latestRemoteBlockInfo);
    $remote = Block::createFromBlockDetail($bitcoinClient->bm_get_block_detail($latestRemoteBlockInfo['hash']));

    if ($queue->diff($remote)) {

        $pool->rollback();  // 立刻回滚临时块

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
            print('10s 后从创世纪块开始处理');
            sleep(10);
        } else if ($needBackof) {    // 高度相同，hash 不同
            Log::notice('高度相同，但是 hash 不同', [
                'height' => $latestBlock->getHeight(),
                'localHash' => $latestBlock->getHash(),
                'remoteHash' => $remote->getHash(),
            ]);
            $detail = $bitcoinClient->bm_get_block_detail($remote->getHash());      // 获取同高度 block
        } else {
            $detail = $bitcoinClient->bm_get_block_detail(strval($latestBlock->getHeight() + 1));
        }

        Log::info(sprintf('当前高度 %d，目标高度 %d', $latestBlock->getHeight(), $detail['height']));

        $block = Block::createFromBlockDetail($detail);

        Log::debug('开始执行 digest');

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
        $gbt = $bitcoinClient->getblocktemplate();
        $gbtPrevHash = $gbt['previousblockhash'];

        if ($gbtPrevHash != $queue->getBlock()->getHash()) {    //已出新块，立即处理
            Log::info('获取的 gbt 与当前块 hash 不同，可能已出新块，立即处理', [
                'local_hash' => $queue->getBlock()->getHash(),
                'gtb_prev_hash' => $gbtPrevHash
            ]);
            continue;
        }

        $newTxs = $pool->update(Collection::make($gbt['transactions']));
        if (count($newTxs)) {
            Log::info(sprintf('检测到临时块新交易 %d 个', count($newTxs)));
            $pool->insert($newTxs);
        } else {
            Log::info('暂无新交易信息，等待 10s');
            sleep(10);
        }
    }
}