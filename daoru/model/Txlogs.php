<?php

use Carbon\Carbon;
use Illuminate\Database\Eloquent\Model;

class Txlogs extends Model {

    const ROW_TYPE_FORWARD = 1;
    const ROW_TYPE_ROLLBACK = 2;

    public $fillable = ['handle_status', 'handle_type', 'block_height', 'tx_hash'];

    public static $tableList = null;

    public static function getTableList(){
        if (getenv('ENV') == 'test' || is_null(static::$tableList)) {       //单测需要实时的表目录
            $conn = App::$container->make('capsule')->getConnection();
            $sql = sprintf("SELECT table_name
                FROM information_schema.tables
                WHERE table_type = 'BASE TABLE' AND table_schema='%s' AND table_name like 'txlogs_%%'
                ORDER BY table_name DESC", Config::get('database.name'));
            $rows = $conn->select($sql);
            static::$tableList = array_map(function ($r) {
                return $r['table_name'];
            }, $rows);
        }

        return static::$tableList;
    }

    public static function getLatestTable() {
        $tables = static::getTableList();
        return count($tables) ? $tables[0] : null;
    }

    public static function createNewTable($currentTable) {
        if (is_null($currentTable)) {
            $nextTable = 'txlogs_0000';
        } else {
            list($_, $currentIndex) = explode('_', $currentTable);
            $currentIndex = intval($currentIndex);
            $nextTable = sprintf('txlogs_%04d', ++$currentIndex);
        }

        // 新建表
        $sql = sprintf('create table %s like 0_tpl_txlogs', $nextTable);
        $conn = App::$container->make('capsule')->getConnection();
        $conn->statement($sql);

        array_unshift(static::$tableList, $nextTable);

        Log::info(sprintf('新建了表 %s', $nextTable));

        return $nextTable;
    }

    public function getTable() {
        return $this->table = static::getLatestTable();
    }

    public static function ensureTable() {
        $conn = App::$container->make('capsule')->getConnection();

        $table = static::getLatestTable();
        $rowCount = 0;

        if (!is_null($table)) {
            $rowCount = $conn->selectOne('select count(`id`) as cnt from ' . $table)['cnt'];
        }

        if (is_null($table) || $rowCount >= Config::get('app.txlogs_maximum_rows')) {
            $table = static::createNewTable($table);
        }

        return $table;
    }

    public static function clearTempLogs($tempLogs) {
        if (!count($tempLogs)) {
            return;
        }

        $rows = array_map(function($tx) {
            $ret = [
                'handle_status' => 100,
                'handle_type' => static::ROW_TYPE_ROLLBACK,
                'block_height' => $tx['block_height'],
                'block_timestamp' => $tx['block_timestamp'],
                'tx_hash' => $tx['tx_hash'],
                'created_at' => Carbon::now()->toDateTimeString(),
                'updated_at' => Carbon::now()->toDateTimeString(),
            ];
            return $ret;
        }, $tempLogs);

        $table = static::ensureTable();
        $conn = App::$container->make('capsule')->getConnection();

        Log::debug(sprintf('找到了可用的 txlogs 表 %s', $table));

        $conn->transaction(function() use ($rows) {
            foreach (array_chunk($rows, Config::get('app.batch_insert')) as $subsetRows) {
                static::insert($subsetRows);
            }
        });

        Log::debug('回滚完毕');
    }

    public static function getTempLogs($pageSize = 50) {
        $conn = App::$container->make('capsule')->getConnection();
        $tables = static::getTableList();

        $done = false;
        $ret = [];

        while (!$done && count($tables)) {
            $tbl = array_shift($tables);

            $offset = 0;
            $query = $conn->table($tbl)
                ->orderBy('id', 'desc')
                ->take($pageSize);

            while (!$done && ($txs = $query->skip($offset * $pageSize)->get(['handle_type', 'block_height', 'tx_hash', 'block_timestamp'])) && count($txs)) {
                foreach ($txs as $tx) {
                    if ($tx['handle_type'] === 1 && $tx['block_height'] === -1) {
                        $ret[] = $tx;
                        continue;
                    }
                    $done = true;
                    break;
                }
                $offset++;
            }

        }

        Log::debug(sprintf('获取要需要清理的临时记录，共计 %s 条', count($ret)));

        return $ret;
    }
}