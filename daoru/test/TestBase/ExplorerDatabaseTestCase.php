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

    public function getPDO() {
        return $this->getConnection()->getConnection();
    }
}