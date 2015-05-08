<?php

use Carbon\Carbon;
use Illuminate\Database\Eloquent\Model;

class RawTx extends Model {
    const TABLE_COUNT = 64;

    protected $hash;

    public function getTable() {

    }

    public static function getTableByHash(RawTx $tx) {
        $hash = $tx->tx_hash;
        return sprintf('raw_txs_%04d', hexdec(substr($hash, -2)) % static::TABLE_COUNT);
    }
}