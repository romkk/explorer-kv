<?php

use Carbon\Carbon;
use Illuminate\Support\Collection;

class Block {
    protected $hash = null;
    protected $prevHash = null;
    protected $height = null;
    protected $txs = null;
    protected $hex = null;
    protected $time = null;

    public static function createFromBlockDetail(array $detail) {
        $block = new Block($detail['hash'], $detail['previousblockhash'], $detail['height'], $detail['time']);
        $block->setHex($detail['rawhex']);
        $block->setTxs(array_map(function($tx) use ($block) {
            $t = new Tx($block, $tx['hash']);
            $t->setHex($tx['rawhex']);
            return $t;
        }, $detail['tx']));
        return $block;
    }

    public function __construct($hash, $prevHash, $height, $blockTimestamp) {
        $this->hash = $hash;
        $this->prevHash = $prevHash;
        $this->height = $height;
        $this->blockTimestamp = $blockTimestamp;
    }

    public function toArray() {
        return [
            'height' => $this->getHeight(),
            'hash' => $this->getHash(),
            'prev_hash' => $this->getPrevHash(),
            'time' => $this->getBlockTimestamp(),
        ];
    }

    public function getBlockTimestamp() {
        return $this->blockTimestamp;
    }

    public function setBlockTimestamp($blockTimestamp) {
        $this->blockTimestamp = $blockTimestamp;
    }

    public function getHash() {
        return $this->hash;
    }

    public function getPrevHash() {
        return $this->prevHash;
    }

    public function getHeight() {
        return $this->height;
    }

    public function getTxs() {
        if (is_null($this->txs)) {
            $detail = Bitcoin::make()->bm_get_block_detail($this->hash);
            $txs = array_map(function($tx) {
                $t = new Tx($this, $tx['hash']);
                $t->setHex($tx['rawhex']);
                return $t;
            }, $detail['tx']);
            $this->setTxs($txs);
        }

        return $this->txs;
    }

    public function setTxs($txs) {
        foreach ($txs as $tx) {
            if (!$tx instanceof Tx) {
                throw new Exception('TypeError: setTxs need array elements of Tx type.');
            }
        }
        $this->txs = $txs;
        return $this;
    }

    public function getHex() {
        if (is_null($this->hex)) {
            $this->setHex(RawBlock::where('block_hash', $this->hash)->pluck('hex'));
        }
        return $this->hex;
    }

    public function setHex($hex) {
        $this->hex = $hex;
    }

    public function insert() {
        Log::info('插入新块记录', $this->toArray());
        $now = Carbon::now();
        $conn = App::$container->make('capsule')->getConnection();

        // begin
        //

        // clear temp logs
        Txlogs::clearTempLogs(Txlogs::getTempLogs());   // clearTempLogs 启用了自己的事务（即便失败也能接受）
        Log::info('回滚临时记录完成');

        // 创建所需的 txlogs 表
        Txlogs::ensureTable();

        // begin transaction
        $conn->beginTransaction();

        // update chain id
        RawBlock::where('block_height', $this->getHeight())
            ->orderBy('chain_id', 'desc')
            ->get(['id', 'chain_id'])
            ->each(function(RawBlock $blk) {
                $blk->chain_id++;
                $blk->save();
            });
        Log::info('更新已有块的 chain_id 完成');

        // insert raw block
        $rawBlockId = RawBlock::insertGetId([
            'block_hash' => $this->getHash(),
            'block_height' => $this->getHeight(),
            'chain_id' => 0,
            'hex' => $this->getHex(),
            'created_at' => $now->toDateTimeString(),
        ]);

        Log::info(sprintf('rawblock 表插入完成, table id = %d', $rawBlockId));

        Log::info('开始插入 txs');
        forEach($this->getTxs() as $tx) {
            // insert raw txs
            $tx->insert();

            // insert txlogs
            Txlogs::insert([
                'handle_status' => 100,
                'handle_type' => Txlogs::ROW_TYPE_FORWARD,
                'block_height' => $this->getHeight(),
                'block_timestamp' => $this->getBlockTimestamp(),
                'tx_hash' => $tx->getHash(),
                'created_at' => $now->toDateTimeString(),
                'updated_at' => $now->toDateTimeString(),
            ]);
        }
        Log::info('txs 插入完毕');
        // done
        $conn->commit();
    }

    public function rollback() {}
}