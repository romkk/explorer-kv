<?php

use Carbon\Carbon;

class Tx {
    protected $block = null;
    protected $hash = null;
    protected $hex = null;

    public function __construct(Block $block, $hash) {
        $this->block = $block;
        $this->hash = $hash;
    }

    public function getBlock() {
        return $this->block;
    }

    public function getHash() {
        return $this->hash;
    }

    public function getHex() {
        if (is_null($this->hex)) {
            $rawTx = new RawTx();
            $rawTx->tx_hash = $this->hash;
            $this->hex = $rawTx->newQuery()->where('tx_hash', $this->hash)->pluck('hex');
        }
        return $this->hex;
    }

    public function setHex($hex) {
        $this->hex = $hex;
        return $this;
    }

    public function insert() {
        $rawTx = new RawTx();
        $rawTx->tx_hash = $this->getHash();
        $rawTx->hex = $this->getHex();
        $rawTx->created_at = Carbon::now()->toDateTimeString();
        $rawTx->save();
    }
}