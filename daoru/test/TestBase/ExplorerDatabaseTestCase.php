<?php

abstract class ExplorerDatabaseTestCase extends PHPUnit_Extensions_Database_TestCase {

    private $conn = null;
    private static $pdo = null;

    /**
     * Returns the test database connection.
     *
     * @return PHPUnit_Extensions_Database_DB_IDatabaseConnection
     */
    final function getConnection() {
        if ($this->conn === null) {
            if (self::$pdo == null) {
                $dsn = 'mysql:dbname=' . Config::get('database.name') . ';host=' . Config::get('database.host');
                self::$pdo = new PDO($dsn, Config::get('database.user'), Config::get('database.pass'));
            }
            $this->conn = $this->createDefaultDBConnection(self::$pdo, Config::get('database.name'));
        }

        return $this->conn;
    }

    public function tableExists($table) {
        $stmt = $this->getPDO()->query("
            SELECT table_name
                FROM information_schema.tables
                WHERE table_type = 'BASE TABLE' AND table_schema='" . Config::get('database.name') . "' AND table_name = '$table'
                ORDER BY table_name DESC limit 1");
        return $stmt->fetch() !== false;
    }

    public function tableCreateLike($table, $tpl) {
        return $this->getPDO()->exec("create table $table like $tpl");
    }

    public function tableDelete($table) {
        $sql = sprintf("drop table if exists %s", $table);
        return $this->getPDO()->exec($sql);
    }

    public function tableDeleteLike($tablePattern) {
        $sql = "show tables like '$tablePattern'";
        $stmt = $this->getPDO()->query($sql);
        $rows = $stmt->fetchAll(PDO::FETCH_ASSOC);

        $affected =  0;
        foreach ($rows as $r) {
            $t = array_values($r)[0];
            $sql = sprintf("drop table %s", $t);
            $affected += $this->getPDO()->exec($sql);
        }

        return $affected;
    }

    public function tableInsert($tableName, $rows) {
        $cols = array_keys($rows[0]);
        $sql = sprintf(
            'insert into %s (%s) values (%s)',
            $tableName,
            join(',', $cols),
            join(',', array_map(function($c) {
                return ":$c";
            }, $cols))
        );

        $stmt = $this->getPDO()->prepare($sql);

        $this->getPDO()->beginTransaction();

        foreach ($rows as $r) {
            foreach ($r as $k => $v) {
                $stmt->bindValue(":$k", $v);
            }
            if ($stmt->execute() === false) {
                $this->getPDO()->rollBack();
                return false;
            };
        }

        $this->getPDO()->commit();

        return true;
    }

    public function tableTruncate($tableName) {
        $sql = "truncate $tableName";
        $this->getPDO()->exec($sql);
    }

    public function getPDO() {
        return $this->getConnection()->getConnection();
    }
}