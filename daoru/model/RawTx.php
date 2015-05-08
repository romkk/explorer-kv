<?php

use Carbon\Carbon;
use Illuminate\Database\Eloquent\Model;

class RawTx extends Model {
    const TABLE_COUNT = 64;

    protected $hash;
    public $timestamps = false;

    public function getTable() {
        return static::getTableByHash($this);
    }

    public function save(array $options = array()) {
        $this->id = static::currentId();

        static::moveNextId();

        parent::save();
    }

    public static function currentId() {
        $id = ExplorerMeta::get('raw_tx_id', '0');
        return intval($id);
    }

    public static function moveNextId() {
        $id = static::currentId();
        ExplorerMeta::put('raw_tx_id', ++$id);
    }

    public static function getTableByHash(RawTx $tx) {
        $hash = $tx->tx_hash;
        return sprintf('raw_txs_%04d', hexdec(substr($hash, -2)) % static::TABLE_COUNT);
    }
}