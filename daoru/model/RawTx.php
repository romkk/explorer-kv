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
        $this->id = static::getNextId($this->getTable());
        parent::save();
    }

    public static function getTableByHash($hash) {
        $suffix = hexdec(substr($hash, -2)) % static::TABLE_COUNT;
        return sprintf('raw_txs_%04d', $suffix);
    }

    public static function getTableById($id) {
        $suffix = intval($id / static::TABLE_VOLUME);
        return sprintf('raw_txs_%04d', $suffix);
    }

    public static function getNextId($tableName) {
        $conn = App::$container->make('capsule')->getConnection();

        $maxId = $conn->table($tableName)->max('id');

        if (is_null($maxId)) {
            sscanf($tableName, 'raw_txs_%04d', $index);
            $maxId = $index * 10e8;
        }

        return intval($maxId + 1);
    }

    public static function txExists($txHash){
        $queryInst = new static();
        $queryInst->tx_hash = $txHash;
        return !is_null($queryInst->newQuery()
            ->where('tx_hash', $txHash)->first(['id']));
    }
}