<?php

use Carbon\Carbon;
use Illuminate\Database\Eloquent\Model;

class Txlogs extends Model {
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

        return $nextTable;
    }

    public static function getCurrentTable() {
        $conn = App::$container->make('capsule')->getConnection();
        $sql = "SELECT table_name
                FROM information_schema.tables
                WHERE table_type = 'BASE TABLE' AND table_schema='BitcoinExplorerDB' AND table_name like 'txlogs_%'
                ORDER BY table_name DESC limit 1";
        $latestTable = $conn->selectOne($sql)['table_name'];

        if ($latestTable) {
            Log::debug(sprintf('获取最新 txlogs 表 %s', $latestTable));
        } else {
            Log::debug('当前没有任何 txlogs 表');
        }

        // 检查当前表
        if ($latestTable) {
            $result = $conn->selectOne('select count(`id`) as cnt from ' . $latestTable)['cnt'];
            if ($result < Config::get('app.txlogs_maximum_rows')) {
                Log::debug(sprintf('找到了可用的 txlogs 表 %s', $latestTable));
                return $latestTable;
            }
        }

        // 计算当前表
        $currentTable = self::createNewTable($latestTable);
        Log::debug(sprintf('新建了 txlogs 表 %s', $currentTable));
        return $currentTable;
    }

    public function getTable() {
        return $this->table = static::getCurrentTable();
    }

    public static function clearTempLogs($tempLogs) {
        if (!count($tempLogs)) {
            return;
        }

        $rows = array_map(function(Txlogs $tx) {
            $tx = $tx->toArray();
            unset($tx['id']);
            $tx['handle_type'] = 2;
            $tx['handle_status'] = 100;
            $tx['created_at'] = $tx['updated_at'] = Carbon::now();

            return $tx;
        }, $tempLogs);

        $conn = App::$container->make('capsule')->getConnection();

        $conn->transaction(function() use ($rows) {
            foreach (array_chunk($rows, Config::get('app.batch_insert')) as $subsetRows) {
                static::insert($subsetRows);
            }
        });


        Log::debug('清理完毕');
    }

    public static function getTempLogs($pageSize = 50) {
        $q = static::query()
            ->orderBy('id', 'desc')
            ->take($pageSize);

        $offset = 0;
        $done = false;
        $ret = [];

        while (!$done && ($txs = $q->skip($offset * $pageSize)->get()) && count($txs)) {
            foreach ($txs as $tx) {
                if ($tx->isTempLog()) {
                    $ret[] = $tx;
                    continue;
                }
                $done = true;
                break;
            }

            $offset++;
        }

        Log::debug(sprintf('获取要需要清理的临时记录，共计 %s 条', count($ret)));
        return $ret;
    }

    public function isTempLog() {
        return $this->handle_type === 1 && $this->block_height === -1;
    }
}