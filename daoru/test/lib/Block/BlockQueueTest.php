<?php

use Carbon\Carbon;
use Illuminate\Support\Collection;

class BlockQueueTest extends ExplorerDatabaseTestCase {

    public function testGetLocalFirstBlock() {
        $this->assertNotNull($block = BlockQueue::getLocalBlockInfo());
        $this->assertEquals(-1, $block['block_height']);
        $this->assertNull($block['block_hash']);
    }

    public function testGetLatestLocalBlock() {
        $this->tableInsert('0_raw_blocks', [
            ['id' => 1, 'block_hash' => 'block_hash_1', 'block_height' => 0, 'chain_id' => 0, 'hex' => 'hex1', 'created_at' => Carbon::now()->toDateTimeString()],
            ['id' => 2, 'block_hash' => 'block_hash_2', 'block_height' => 1, 'chain_id' => 0, 'hex' => 'hex2', 'created_at' => Carbon::now()->toDateTimeString()],
        ]);

        $this->assertNotNull($block = BlockQueue::getLocalBlockInfo());
        $this->assertEquals(1, $block['block_height']);
        $this->assertEquals(2, $block['id']);

        $this->tableTruncate('0_raw_blocks');
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

    public function testMake() {
        $queue = BlockQueue::make();
        $this->assertEquals(0, $queue->length());

        $this->tableInsert('0_raw_blocks', [
            ['id' => 1, 'block_hash' => '000000000000226f7618566e70a2b5e020e29579b46743f05348427239bf41a1', 'block_height' => 300000, 'chain_id' => 0, 'hex' => 'hex1', 'created_at' => Carbon::now()->toDateTimeString()],
            ['id' => 2, 'block_hash' => '0000000000004829474748f3d1bc8fcf893c88be255e6d7f571c548aff57abf4', 'block_height' => 300001, 'chain_id' => 0, 'hex' => 'hex2', 'created_at' => Carbon::now()->toDateTimeString()],
        ]);

        $queue = BlockQueue::make();
        $this->assertEquals(2, $queue->length());

        $this->tableTruncate('0_raw_blocks');
    }

    public function testPush() {
        $queue = new BlockQueue(new Collection(), 1);
        $this->assertEquals(0, $queue->length());

        $queue->push(new Block('hash', 'prevhash', 0, 1));
        $this->assertEquals(1, $queue->length());
        $this->assertEquals('hash', $queue->getBlock()->getHash());

        $queue->push(new Block('hash2', 'prevhash2', 1, 1));
        $this->assertEquals(1, $queue->length());
        $this->assertEquals('hash2', $queue->getBlock()->getHash());
    }

    public function testGetBlock() {
        $queue = new BlockQueue(new Collection([
            new Block('hash1', 'prevhash1', 0, 1),
            new Block('hash2', 'prevhash2', 1, 1),
        ]));

        $this->assertEquals(1, $queue->getBlock()->getHeight());
        $this->assertEquals(0, $queue->getBlock(-2)->getHeight());

        $queue = new BlockQueue(new Collection());

        $this->assertNull($queue->getBlock());
    }

    public function testDigestEmptyQueue() {
        $queue = new BlockQueue(new Collection());
        $remote = new Block('00000000ce07e1de4bec68ee5055259d5d64cdf4b23b0317135c1955296cab3d', '000000000000e6ae2516b4249f14f368dd57adc23d0f7c8a6c615f5cc0d50db8', 300003, 1412902068);
        $queue->digest($remote, $newBlock, $orphanBlocks);
        $this->assertEquals(0, count($orphanBlocks));
        $this->assertEquals($remote, $newBlock);
    }

    public function testDigestNoOrphan() {
        $queue = new BlockQueue(new Collection([
            new Block('000000000000226f7618566e70a2b5e020e29579b46743f05348427239bf41a1', '00000000dfe970844d1bf983d0745f709368b5c66224837a17ed633f0dabd300', 300000, 1412899877),
            new Block('0000000000004829474748f3d1bc8fcf893c88be255e6d7f571c548aff57abf4', '000000000000226f7618566e70a2b5e020e29579b46743f05348427239bf41a1', 300001, 1412900707),
            new Block('000000000000e6ae2516b4249f14f368dd57adc23d0f7c8a6c615f5cc0d50db8', '0000000000004829474748f3d1bc8fcf893c88be255e6d7f571c548aff57abf4', 300002, 1412900867),
        ]));

        $remote = new Block('00000000ce07e1de4bec68ee5055259d5d64cdf4b23b0317135c1955296cab3d', '000000000000e6ae2516b4249f14f368dd57adc23d0f7c8a6c615f5cc0d50db8', 300003, 1412902068);
        $this->assertTrue($queue->digest($remote, $newBlock, $orphanBlocks));
        $this->assertEquals(0, count($orphanBlocks));
        $this->assertEquals($remote, $newBlock);
    }

    public function testDigestOrphanBlockFound() {
        $queue = new BlockQueue(new Collection([
            new Block('000000000000226f7618566e70a2b5e020e29579b46743f05348427239bf41a1', '00000000dfe970844d1bf983d0745f709368b5c66224837a17ed633f0dabd300', 300000, 1412899877),
            new Block('fakehash', '000000000000226f7618566e70a2b5e020e29579b46743f05348427239bf41a1', 300001, 1412900707),
            new Block('fakehash2', 'fakehash', 300002, 1412900867),
        ]));

        $remote = new Block('00000000ce07e1de4bec68ee5055259d5d64cdf4b23b0317135c1955296cab3d', '000000000000e6ae2516b4249f14f368dd57adc23d0f7c8a6c615f5cc0d50db8', 300003, 1412900867);
        $queue->digest($remote, $newBlock, $orphanBlocks);
        $this->assertEquals(2, count($orphanBlocks));
        $this->assertEquals('0000000000004829474748f3d1bc8fcf893c88be255e6d7f571c548aff57abf4', $newBlock->getHash());
    }

    public function testDigestOrphanBlockNotFound() {
        $queue = new BlockQueue(new Collection([
            new Block('fakehashh', 'fakehhhh', 300000, 1412899877),
            new Block('fakehash', 'fakehashh', 300001, 1412900707),
            new Block('fakehash2', 'fakehash', 300002, 1412900867),
        ]));

        $remote = new Block('00000000ce07e1de4bec68ee5055259d5d64cdf4b23b0317135c1955296cab3d', '000000000000e6ae2516b4249f14f368dd57adc23d0f7c8a6c615f5cc0d50db8', 300003, 1412900867);
        $this->assertFalse($queue->digest($remote, $newBlock, $orphanBlocks));
    }

    public function testDiff() {
        $queue = new BlockQueue(new Collection([
            new Block('hash1', 'prevhash1', 0, 1),
            new Block('hash2', 'prevhash2', 1, 1),
            new Block('hash3', 'prevhash3', 2, 1),
        ]));

        $remote = new Block('hash4', 'hash3', 3, 1);
        $this->assertTrue($queue->diff($remote));

        $remote = new Block('hash3', 'prevhash3', 2, 1);
        $this->assertFalse($queue->diff($remote));

        $remote = new Block('hash4', 'hash3', 2, 1);
        $this->assertTrue($queue->diff($remote));
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