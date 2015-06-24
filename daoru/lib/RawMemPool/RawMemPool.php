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
        $hashList = $this->queue->map(function(Tx $tx) {
            return $tx->getHash();
        })->toArray();

        // assert memory pool matches gbt
        assert(count(array_diff($hashList, $txDataList->lists('hash'))) == 0, 'GBT results should contains all the txs in the mempool');

        Log::info(sprintf('[mempool] count = %d', $this->length()), $hashList);
        $diffHash = [];

        $diff = $txDataList->filter(function($txData) use (&$hashList, &$diff) {
            return !in_array($txData['hash'], $hashList);
        })->map(function($txData) use (&$diffHash) {
            $tx = new Tx($this->fakeBlock, $txData['hash']);
            $tx->setHex($txData['data']);
            $diffHash[] = $txData['hash'];
            return $tx;
        });

        $this->queue = $this->queue->merge($diff);

        Log::info(sprintf('[mempool] diff count = %d', count($diff)), $diffHash);

        return $diff;
    }

    public function rollback() {
        Txlogs::clearTempLogs(Txlogs::getTempLogs());
        Txlogs::ensureTable();

        while ($this->queue->count()) {
            $this->queue->pop();
        }

        Log::info(sprintf('[mempool] clear up, count = %d', $this->length()));
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