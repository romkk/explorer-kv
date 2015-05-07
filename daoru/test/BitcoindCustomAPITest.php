<?php

class BitcoindCustomAPITest extends PHPUnit_Framework_TestCase {
    private $client;

    protected function setUp() {
        $this->client = Bitcoin::make();
    }

    public function testGetBestBlock() {
        $result = $this->client->bm_get_best_block();

        $this->assertArrayHasKey('hash', $result);
        $this->assertArrayHasKey('height', $result);
        $this->assertGreaterThan(0, $result['height']);
        $this->assertEquals(64, strlen($result['hash']));
    }

    public function testGetBlockDetail() {
        $result = $this->client->bm_get_block_detail('00000000aba3469119e413c750b26e4821c84cc323b7282b0e50130da49221c5');

        $this->assertArrayHasKey('rawhex', $result);
        $this->assertArrayHasKey('rawhex', $result['tx'][0]);
    }

}