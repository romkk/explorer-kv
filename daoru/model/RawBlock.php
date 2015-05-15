<?php

use Carbon\Carbon;
use Illuminate\Database\Eloquent\Model;

class RawBlock extends Model {
    protected $table = '0_raw_blocks';

    protected $fillable = ['block_hash', 'block_height', 'chain_id', 'hex', 'created_at'];
    public $timestamps = false;

    public static function findLatestBlock() {
        return static::orderBy('id', 'desc')
            ->first(['id', 'block_hash', 'block_height', 'chain_id', 'created_at']);
    }
}