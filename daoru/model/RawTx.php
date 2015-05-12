<?php

use Carbon\Carbon;
use Illuminate\Database\Eloquent\Model;

class RawTx extends Model {
    const TABLE_COUNT = 64;
    const TABLE_VOLUME = 10e8;

    public $timestamps = false;
    public $incrementing = false;

    public $fillable = ['tx_hash', 'hex', 'created_at', 'id'];

    public function getTable() {
        if (is_null($this->tx_hash)) {
            throw new Exception('RawTx tx_hash not specified');
        }
        return static::getTableByHash($this->tx_hash);
    }

    public function save(array $options = array()) {
        $this->id = $this->getId();
        parent::save();
    }

    public function getId() {
        if (!is_null($this->id)) {
            return $this->id;
        }

        $maxId = static::max('id') ?: 0;

        return ++$maxId;
    }

    public static function getTableByHash($hash) {
        $suffix = hexdec(substr($hash, -2)) % static::TABLE_COUNT;
        return sprintf('raw_txs_%04d', $suffix);
    }

    public static function getTableById($id) {
        $suffix = intval($id / static::TABLE_VOLUME);
        return sprintf('raw_txs_%04d', $suffix);
    }
}