<?php

class EnvTest extends PHPUnit_Framework_TestCase {
    public function testConnectMysql() {
        $results = Illuminate\Database\Capsule\Manager::connection()->select('select 1 + 1 as sumup');
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