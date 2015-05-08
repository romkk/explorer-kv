<?php

use Carbon\Carbon;

class BlockTest extends ExplorerDatabaseTestCase {

    public function testGetLocalFirstBlock() {
        $this->assertNotNull($block = BlockQueue::getLocalBlockInfo());
        $this->assertEquals(-1, $block['block_height']);
        $this->assertNull($block['block_hash']);
    }

    public function testGetLatestLocalBlock() {
        $this->tableInsert('0_raw_blocks', [
            [
                'id' => 1,
                'block_hash' => 'block_hash_1',
                'block_height' => 0,
                'chain_id' => 0,
                'hex' => 'hex1',
                'created_at' => Carbon::now()->toDateTimeString()
            ],
            [
                'id' => 2,
                'block_hash' => 'block_hash_2',
                'block_height' => 1,
                'chain_id' => 0,
                'hex' => 'hex2',
                'created_at' => Carbon::now()->toDateTimeString()
            ],
        ]);

        $this->assertNotNull($block = BlockQueue::getLocalBlockInfo());
        $this->assertEquals(1, $block['block_height']);
        $this->assertEquals(2, $block['id']);
    }

    public function testGetLatestRemoteBlock() {
        $this->assertNotNull($block = BlockQueue::getRemoteBlockInfo());
        $this->assertArrayHasKey('block_height', $block);
        $this->assertArrayHasKey('block_hash', $block);
        $this->assertArrayNotHasKey('hash', $block);
        $this->assertArrayNotHasKey('height', $block);
        $this->assertGreaterThan(0, $block['block_height']);
        $this->assertGreaterThan(0, strlen($block['block_height']));
    }

    /**
     * Returns the test dataset.
     *
     * @return PHPUnit_Extensions_Database_DataSet_IDataSet
     */
    protected function getDataSet() {
        return new DbUnit_ArrayDataSet([
            '0_raw_blocks' => [],
        ]);
    }
}