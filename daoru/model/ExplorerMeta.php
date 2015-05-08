<?php

use Illuminate\Database\Eloquent\Model;

class ExplorerMeta extends Model {
    protected $table = '0_explorer_meta';
    protected $hidden = ['id'];
    protected $fillable = ['key', 'value'];

    public static function get($k, $default = null) {
        $ret = static::where('key', strval($k))->pluck('value');
        return $ret ?: $default;
    }

    public static function put($k, $v) {
        return static::updateOrCreate(
            ['key' => strval($k)],
            ['value' => strval($v)]
        );
    }
}