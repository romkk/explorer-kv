<?php

class EnvTest extends PHPUnit_Framework_TestCase {
    private $conn;

    public function setUp() {
        $this->conn = Illuminate\Database\Capsule\Manager::connection();
    }

    public function testConnectMysql() {
        $results = $this->conn->select('select 1 + 1 as sumup');
        $this->assertEquals(1, count($results));
        $this->assertEquals('2', $results[0]['sumup']);
    }

    public function testConnectBitcoind() {
        $client = Bitcoin::make();
        try {
            $client->ping();
        } catch (BitcoindException $e) {
            $this->fail('Cannot connect to bitcoind');
        }
    }
}