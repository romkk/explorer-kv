<?php

use Carbon\Carbon;
use Illuminate\Support\Collection;

class BlockTest extends ExplorerDatabaseTestCase {

    /**
     * Returns the test dataset.
     *
     * @return PHPUnit_Extensions_Database_DataSet_IDataSet
     */
    protected function getDataSet() {
        return new DbUnit_ArrayDataSet([]);
    }

    public function testGetHex() {
        $this->tableInsert('0_raw_blocks', [
            ['id' => 1, 'block_hash' => 'block_hash', 'block_height' => 0, 'chain_id' => 0, 'hex' => 'hex', 'created_at' => Carbon::now()->toDateTimeString()],
        ]);

        $block = new Block('block_hash', 'prev_hash', 0, 1);
        $this->assertEquals('hex', $block->getHex());

        $block = new Block('block_hash', 'prev_hash', 0, 1);
        $block->setHex('hex2');
        $this->assertEquals('hex2', $block->getHex());

        $this->tableTruncate('0_raw_blocks');
    }

    public function testGetTxs() {
        $this->tableInsert('0_raw_blocks', [
            ['id' => 1, 'block_hash' => 'block_hash', 'block_height' => 0, 'chain_id' => 0, 'hex' => 'hex', 'created_at' => Carbon::now()->toDateTimeString()],
        ]);

        $block = new Block('block_hash', 'prev_hash', 0, 1);
        $block->setTxs([]);
        $this->assertEquals([], $block->getTxs());

        $block = new Block(
            '00000000b4061b7d09fa7e2f1c9ada6f674c22ba7a0eac6d158b808925c0c766',
            '0000000051b02474c2c68fbbeda6ffd64e00ba518465104605b132e10ee8a266',
            394772,
            1
        );
        $txs = $block->getTxs();
        $this->assertEquals(1, count($txs));
        $this->assertEquals('d7cd58480b6e0363eafb32e4ac778ffeaafd144cb690a2756bf527d78ff49c97', $txs[0]->getHash());

        $this->tableTruncate('0_raw_blocks');
    }
}