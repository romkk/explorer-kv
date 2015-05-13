<?php

use Carbon\Carbon;
use Illuminate\Support\Collection;

class BlockInsertTest extends ExplorerDatabaseTestCase {

    public function setUp() {
        $this->tableCreateLike('txlogs_0000', '0_tpl_txlogs');
        $this->tableCreateLike('txlogs_0001', '0_tpl_txlogs');
        parent::setUp();
    }

    public function tearDown(){
        $this->tableDeleteLike('txlogs_%');
        parent::tearDown();
    }

    protected function getDataSet() {
        return new DbUnit_ArrayDataSet([
            'txlogs_0000' => [
                ['id' => 1, 'handle_status' => 100, 'handle_type' => 1, 'block_height' => 0, 'tx_hash' => 'hash1', 'block_timestamp' => 1, 'created_at' => Carbon::now()->toDateTimeString(), 'updated_at' => Carbon::now()->toDateTimeString(),],
                ['id' => 2, 'handle_status' => 100, 'handle_type' => 1, 'block_height' => -1, 'tx_hash' => 'hash2', 'block_timestamp' => 1, 'created_at' => Carbon::now()->toDateTimeString(), 'updated_at' => Carbon::now()->toDateTimeString(),],
            ],
            'txlogs_0001' => [
                ['id' => 1, 'handle_status' => 100, 'handle_type' => 1, 'block_height' => -1, 'tx_hash' => 'hash3', 'block_timestamp' => 1, 'created_at' => Carbon::now()->toDateTimeString(), 'updated_at' => Carbon::now()->toDateTimeString(),],
            ],
            '0_raw_blocks' => [
                ['id' => 1, 'block_hash' => 'hasha', 'block_height' => 0, 'chain_id' => 0, 'hex' => 'hex', 'created_at' => Carbon::now()->toDateTimeString(),],
                ['id' => 2, 'block_hash' => 'hashb', 'block_height' => 0, 'chain_id' => 1, 'hex' => 'hex', 'created_at' => Carbon::now()->toDateTimeString(),],
            ],
            'raw_txs_0000' => [],
        ]);
    }

    public function testInsert() {
        $tmp = Config::get('app.txlogs_maximum_rows');
        Config::put('app.txlogs_maximum_rows', 1);

        $block = new Block('newhash', 'hasha', 0, 1);
        $block->setHex('newhex');
        $t1 = new Tx($block, 'txhash100');
        $t1->setHex('hex1');
        $t2 = new Tx($block, 'txhash200');
        $t2->setHex('hex2');
        $block->setTxs([$t1, $t2]);
        $block->insert();

        $this->assertTableRowCount('txlogs_0001', 1);   //回滚 +2
        $this->assertTableRowCount('txlogs_0002', 2);   //新建表
        $rows = RawBlock::orderBy('id', 'desc')->get();
        $this->assertEquals(3, count($rows));
        $this->assertEquals($rows[0]->block_hash, 'newhash');
        $this->assertEquals(2, $rows[1]->chain_id);
        $this->assertEquals(1, $rows[2]->chain_id);
        $this->assertTableRowCount('raw_txs_0000', 2);

        Config::put('app.txlogs_maximum_rows', $tmp);
    }
}