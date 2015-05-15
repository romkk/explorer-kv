<?php

use Carbon\Carbon;

class RawBlocksTest extends ExplorerDatabaseTestCase {

    public function testFindLatestBlockEmpty() {
        $this->assertNull(RawBlock::findLatestBlock());
    }

    public function testFindLatestBlockOne() {
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
        $this->assertTableRowCount('0_raw_blocks', 2);

        $this->assertNotNull($block = RawBlock::findLatestBlock());
        $this->assertEquals(2, $block['id']);
        $this->assertEquals(1, $block['block_height']);
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