<?php

use Carbon\Carbon;
use Illuminate\Support\Collection;

class RawMemPool {

    protected $fakeBlock = null;
    protected $queue = null;
    protected $client = null;
    protected $conn = null;

    public function __construct() {
        $this->fakeBlock = new Block('', '', -1, 0);
        $this->queue = new Collection();
        $this->client = Bitcoin::make();
        $this->conn = App::$container->make('capsule')->getConnection();
    }

    public function length() {
        return $this->queue->count();
    }

    public function update(Collection $txDataList) {
        Log::info(sprintf('Local Mempool count = %d', $this->length()));
        $hashList = $this->queue->map(function(Tx $tx) {
            return $tx->getHash();
        })->toArray();

        $diff = $txDataList->filter(function($txData) use (&$hashList, &$diff) {
            return !in_array($txData['hash'], $hashList);
        })->map(function($txData) {
            $tx = new Tx($this->fakeBlock, $txData['hash']);
            $tx->setHex($txData['data']);
            return $tx;
        });

        $this->queue = $this->queue->merge($diff);

        return $diff;
    }

    public function rollback() {
        Txlogs::clearTempLogs(Txlogs::getTempLogs());
        Txlogs::ensureTable();

        while ($this->queue->count()) {
            $this->queue->pop();
        }
    }

    public function insert($newTxs) {
        Txlogs::ensureTable();

        $this->conn->transaction(function() use ($newTxs) {
            foreach ($newTxs as $tx) {
                $now = Carbon::now()->toDateTimeString();
                $row = [
                    'handle_status' => 100,
                    'handle_type' => Txlogs::ROW_TYPE_FORWARD,
                    'block_height' => -1,
                    'block_id' => -1,
                    'block_timestamp' => time(),
                    'tx_hash' => $tx->getHash(),
                    'created_at' => $now,
                    'updated_at' => $now,
                ];
                Txlogs::insert($row);

                $tx->insert();
            }

        });
    }
}