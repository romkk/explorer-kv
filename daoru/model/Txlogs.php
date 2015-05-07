<?php

use Illuminate\Database\Eloquent\Model;

class Txlogs extends Model {
    protected $tableName = null;

    private static $currentTable = null;

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
        Log::debug(sprintf('新建了 txlogs 表 %s', self::$currentTable));
        return $currentTable;
    }

    public function setTableName($name) {
        $this->tableName = $name;
    }
}