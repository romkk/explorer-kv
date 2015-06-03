<?php

use Carbon\Carbon;
use Illuminate\Support\Collection;

class RawMemPoolTest extends ExplorerDatabaseTestCase {

    public function setUp() {
        $this->tableCreateLike('txlogs_0000', '0_tpl_txlogs');

        parent::setUp();
    }

    public function tearDown() {
        $this->tableDeleteLike('txlogs_%');

        parent::tearDown();
    }

    /**
     * Returns the test dataset.
     *
     * @return PHPUnit_Extensions_Database_DataSet_IDataSet
     */
    protected function getDataSet() {
        return new DbUnit_ArrayDataSet([
            'txlogs_0000' => [],
            'raw_txs_0030' => [],
            'raw_txs_0048' => [],
        ]);
    }
    
    public function testUpdate() {
        $pool = new RawMemPool();
        $this->assertEquals(0, $pool->length());

        $txList = Collection::make([
            [
                'data' => 'data',
                'hash' => '2e997db6507ca12ec7b2182d8af47e1424d3342f1f5c3532d3289d7c82b6adb0',
            ],
            [
                'data' => 'data',
                'hash' => '22de62bd5f54f9ddf41b0e50e87f2638186548111ac3d4c3d5c3049373bfa7c3'
            ],
        ]);
        $newTxs = $pool->update($txList);

        $this->assertEquals(2, count($newTxs));
        $this->assertEquals('2e997db6507ca12ec7b2182d8af47e1424d3342f1f5c3532d3289d7c82b6adb0', $newTxs[0]->getHash());
        $this->assertEquals('22de62bd5f54f9ddf41b0e50e87f2638186548111ac3d4c3d5c3049373bfa7c3', $newTxs[1]->getHash());

        $txList = Collection::make([
            [
                'hash' => '2e997db6507ca12ec7b2182d8af47e1424d3342f1f5c3532d3289d7c82b6adb0',     //1
                'data' => 'data'
            ],
            [
                'hash' => '929090a8c4ceb0822ab07f68d1e333a14cd9db8a354d31232cb939a5e391d89e',     //2
                'data' => 'data'
            ],
            [
                'hash' => '22de62bd5f54f9ddf41b0e50e87f2638186548111ac3d4c3d5c3049373bfa7c3',        //3
                'data' => 'data'
            ],
            [
                'hash' => '2b936954f8b25cb7da771af20acc8b609a3828f56ca6c6b2c776991464224e1b',        //4
                'data' => 'data'
            ],
            [
                'hash' => '9779f87030e25eaa7516f8ee335de6764db54120f2c3797f90e64d21808ce37b',     //5
                'data' => 'data'
            ],
        ]);
        $newTxs = $pool->update($txList);

        $this->assertEquals(3, count($newTxs));
        $this->assertEquals('929090a8c4ceb0822ab07f68d1e333a14cd9db8a354d31232cb939a5e391d89e', $newTxs[0]->getHash());
        $this->assertEquals('2b936954f8b25cb7da771af20acc8b609a3828f56ca6c6b2c776991464224e1b', $newTxs[1]->getHash());
        $this->assertEquals('9779f87030e25eaa7516f8ee335de6764db54120f2c3797f90e64d21808ce37b', $newTxs[2]->getHash());

        $txList = Collection::make([
            [
                'hash' => '2e997db6507ca12ec7b2182d8af47e1424d3342f1f5c3532d3289d7c82b6adb0',     //1
                'data' => 'data'
            ],
            [
                'hash' => '929090a8c4ceb0822ab07f68d1e333a14cd9db8a354d31232cb939a5e391d89e',     //2
                'data' => 'data'
            ],
            [
                'hash' => '22de62bd5f54f9ddf41b0e50e87f2638186548111ac3d4c3d5c3049373bfa7c3',        //3
                'data' => 'data'
            ],
            [
                'hash' => '2b936954f8b25cb7da771af20acc8b609a3828f56ca6c6b2c776991464224e1b',        //4
                'data' => 'data'
            ],
            [
                'hash' => '9779f87030e25eaa7516f8ee335de6764db54120f2c3797f90e64d21808ce37b',     //5
                'data' => 'data'
            ],
            [
                'hash' => 'a7e8f29d37f2c46c2f81ddb880950d8b52c1b9f56e64956da1ea036109cc36d0',     //6
                'data' => 'data'
            ]
        ]);
        $newTxs = $pool->update($txList);

        $this->assertEquals(1, count($newTxs));
        $this->assertEquals('a7e8f29d37f2c46c2f81ddb880950d8b52c1b9f56e64956da1ea036109cc36d0', $newTxs[0]->getHash());
    }
    
    public function testInsert() {
        $pool = new RawMemPool();
        $newTxs = $pool->update(Collection::make([
            [
                'hash' => '2e997db6507ca12ec7b2182d8af47e1424d3342f1f5c3532d3289d7c82b6adb0',     //1
                'data' => 'data'
            ],
            [
                'hash' => '929090a8c4ceb0822ab07f68d1e333a14cd9db8a354d31232cb939a5e391d89e',     //2
                'data' => 'data'
            ],
        ]));

        $pool->insert($newTxs);

        $this->assertTableRowCount('txlogs_0000', 2);
        $this->assertTableRowCount('raw_txs_0048', 1);
        $this->assertTableRowCount('raw_txs_0030', 1);
    }

    public function testRollback(){
        $now = Carbon::now()->toDateTimeString();
        $this->tableInsert('txlogs_0000', [
            [ 'id' => 1, 'handle_status' => 100, 'handle_type' => Txlogs::ROW_TYPE_FORWARD, 'block_height' => -1, 'block_timestamp' => 1, 'tx_hash' => '2e997db6507ca12ec7b2182d8af47e1424d3342f1f5c3532d3289d7c82b6adb0', 'created_at' => $now, 'updated_at' => $now, ],
            [ 'id' => 2, 'handle_status' => 100, 'handle_type' => Txlogs::ROW_TYPE_FORWARD, 'block_height' => -1, 'block_timestamp' => 2, 'tx_hash' => '929090a8c4ceb0822ab07f68d1e333a14cd9db8a354d31232cb939a5e391d89e', 'created_at' => $now, 'updated_at' => $now, ],
        ]);

        $pool = new RawMemPool();
        $pool->update(Collection::make([
            [
                'hash' => '2e997db6507ca12ec7b2182d8af47e1424d3342f1f5c3532d3289d7c82b6adb0',     //1
                'data' => 'data'
            ],
            [
                'hash' => '929090a8c4ceb0822ab07f68d1e333a14cd9db8a354d31232cb939a5e391d89e',     //2
                'data' => 'data'
            ],
        ]));

        $this->assertEquals(2, $pool->length());

        $pool->rollback();

        $this->assertEquals(0, $pool->length());
        $this->assertTableRowCount('txlogs_0000', 4);

        $this->tableTruncate('txlogs_0000');
    }
}