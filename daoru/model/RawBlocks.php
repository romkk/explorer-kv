<?php

use Carbon\Carbon;
use Illuminate\Database\Eloquent\Model;

class RawBlocks extends Model {
    protected $table = '0_raw_blocks';

    public static function findLatestBlock() {
        return static::orderBy('id', 'desc')
            ->first(['id', 'block_hash', 'block_height', 'chain_id', 'created_at']);
    }
}