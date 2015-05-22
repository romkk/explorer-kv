<?php

use Carbon\Carbon;
use Illuminate\Support\Collection;

class BlockQueueTest extends ExplorerDatabaseTestCase {

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

    public function testRollback() {
        $this->tableCreateLike('txlogs_0000', '0_tpl_txlogs');

        $client = Bitcoin::make();
        $orphanBlocks = new Collection([
            Block::createFromBlockDetail($client->bm_get_block_detail('000000000000226f7618566e70a2b5e020e29579b46743f05348427239bf41a1')),
            Block::createFromBlockDetail($client->bm_get_block_detail('0000000000004829474748f3d1bc8fcf893c88be255e6d7f571c548aff57abf4')),
            Block::createFromBlockDetail($client->bm_get_block_detail('000000000000e6ae2516b4249f14f368dd57adc23d0f7c8a6c615f5cc0d50db8')),
        ]);

        $queue = new BlockQueue(Collection::make());
        $queue->rollback($orphanBlocks);

        $this->assertTableRowCount('txlogs_0000', 24 + 1 + 2);
        $rows = Txlogs::orderBy('id')->get();
        $this->assertEquals('d52d722021d7e4b6b78fbaef6934b439e9e58f37b2914d18ba45643bbe7485e9', $rows[0]->tx_hash);
        $this->assertEquals(2, $rows[0]->handle_type);
        $this->assertEquals('a97d821841465238b48a175552cf27bdc527a42b0f80cf75c45ac76d1eba6c85', $rows[1]->tx_hash);
        $this->assertEquals('f673eedac6c74513e0bab8d3757849a4edb723847fe4b824547813f2a9cedfd2', $rows[24]->tx_hash);
        $this->assertEquals('9e109b6b9ca374d573f9e923490076980d612d7ea75c62b6209b2001ff36f38c', $rows[26]->tx_hash);

        $this->tableDeleteLike('txlogs_%');
    }

    /**
     * Returns the test dataset.
     *
     * @return PHPUnit_Extensions_Database_DataSet_IDataSet
     */
    protected function getDataSet() {
        return new DbUnit_ArrayDataSet([
            '0_raw_blocks' => [],
            '0_explorer_meta' => [],
        ]);
    }
}