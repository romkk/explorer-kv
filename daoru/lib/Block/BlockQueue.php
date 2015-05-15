<?php

use Illuminate\Support\Collection;

class BlockQueue {

    protected $queue = null;
    protected $queueLength = null;

    public function __construct(Collection $localBlocks, $queueLength = 50) {
        foreach ($localBlocks as $blk) {
            if (!$blk instanceof Block) {
                throw new Exception('TypeError: BlockQueue construct need array elements of Block type.');
            }
        }
        $this->queue = new Collection($localBlocks);
        $this->queueLength = $queueLength;
    }

    public function push(Block $block) {
        $this->queue->push($block);
        if ($this->length() > $this->queueLength) {
            $this->queue->shift();
        }
    }

    public function length() {
        return count($this->queue);
    }

    public function getBlock($offset = -1) {
        if ($offset < 0) {
            $offset = $this->length() + $offset;
        }
        return $offset < 0 ? null : $this->queue[$offset];
    }

    public function rollback(Collection $orphanBlocks) {
        Txlogs::ensureTable();

        App::$container->make('capsule')->getConnection()->transaction(function() use ($orphanBlocks) {
            $orphanBlocks->reverse()->each(function (Block $block) {
                $block->rollback();
            });
        });
    }

    public function diff(Block $remote) {
        $block = $this->getBlock() ?: new Block(null, null, -1, 1);
        return $block->getHeight() < $remote->getHeight() ||
            $block->getHeight() === $remote->getHeight() && $block->getHash() !== $remote->getHash();
    }

    public function digest(Block $remote, &$newBlock, &$orphanBlocks, $backoff = 0) {
        $bitcoinClient = Bitcoin::make();
        $rollbackOffset = $backoff;

        if ($this->length() === 0) {        //第一次初始化，无块
            $this->push($newBlock = $remote);
            $orphanBlocks = new Collection();
            return true;
        }

        while ($rollbackOffset < $this->length()) {
            $localPointer = $this->getBlock(-$rollbackOffset - 1);
            $currentHeight = $remote->getHeight() -  $rollbackOffset;
            if ($rollbackOffset === 0) {
                $newBlock = $remote;
            }  else {
                $detail = $bitcoinClient->getBlockByHeight($currentHeight);
                $newBlock = Block::createFromBlockDetail($detail);
            }

            if ($localPointer->getHash() === $newBlock->getPrevHash()) {      //命中 block，计算出 orphan block
                $orphanBlocks = $this->queue->splice($this->length() - $rollbackOffset, $rollbackOffset);
                $this->push($newBlock);
                return true;
            } else {        // miss
                $rollbackOffset++;
            }
        }

        if (getenv('ENV') === 'test') {
            return false;
        } else {
            Log::error(sprintf('diff offset 超过预设的 %d，进程退出。', $this->queueLength));
            exit(1);
        }
    }

    public static function make() {
        $blocks = RawBlock::where('chain_id', 0)
            ->orderBy('id', 'desc')
            ->take(50)
            ->get(['id', 'block_hash', 'block_height', 'chain_id', 'created_at'])
            ->reverse()
            ->map(function (RawBlock $block) {
                $detail = Bitcoin::make()->bm_get_block_detail($block->block_hash);
                if (!array_key_exists('previousblockhash', $detail)) {
                    $detail['previousblockhash'] = '';
                }
                return Block::createFromBlockDetail($detail);
            });

        return new static($blocks);
    }

    public static function getRemoteBlockInfo() {
        $i = Bitcoin::make()->bm_get_best_block();
        return [
            'block_height' => $i['height'],
            'block_hash' => $i['hash'],
        ];
    }

    public static function getLocalBlockInfo() {
        $i = RawBlock::findLatestBlock();

        if (is_null($i)) {
            return [
                'block_height' => -1,
                'block_hash' => null
            ];
        }

        return $i->toArray();
    }
}