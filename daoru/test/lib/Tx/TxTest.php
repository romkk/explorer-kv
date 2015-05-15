<?php

use Carbon\Carbon;
use Illuminate\Support\Collection;

class TxTest extends ExplorerDatabaseTestCase {

    /**
     * Returns the test dataset.
     *
     * @return PHPUnit_Extensions_Database_DataSet_IDataSet
     */
    protected function getDataSet() {
        return new DbUnit_ArrayDataSet([
            'raw_txs_0000' => [
                [ 'id' => 1, 'tx_hash' => '0d4e9a679dea16607e02e299660571114efc730846c5c94a07bfd2b335e39200', 'hex' => 'hex', 'created_at' => Carbon::now()->toDateTimeString()],
            ],
            'raw_txs_0001' => [],
        ]);
    }

    public function testGetHex() {
        $block = new Block('hash', 'prevhash', 0, 1);
        $tx = new Tx($block, '0d4e9a679dea16607e02e299660571114efc730846c5c94a07bfd2b335e39201');
        $this->assertNull($tx->getHex());

        $block = new Block('hash', 'prevhash', 0, 1);
        $tx = new Tx($block, '0d4e9a679dea16607e02e299660571114efc730846c5c94a07bfd2b335e39200');
        $this->assertEquals('hex', $tx->getHex());
    }

    public function testInsert() {
        $block = new Block('hash', 'prevhash', 0, 1);
        $tx = new Tx($block, '0d4e9a679dea16607e02e299660571114efc730846c5c94a07bfd2b335e39100');
        $tx->setHex('hex');
        $tx->insert();

        $this->assertTableRowCount('raw_txs_0000', 2);
    }

    public function testInsertAlreadyExist() {
        $block = new Block('hash', 'prevhash', 0, 1);
        $tx = new Tx($block, '0d4e9a679dea16607e02e299660571114efc730846c5c94a07bfd2b335e39200');
        $tx->setHex('hex');
        $tx->insert();

        $this->assertTableRowCount('raw_txs_0000', 1);
    }

    public function testRollback() {
        $this->tableCreateLike('txlogs_0000', '0_tpl_txlogs');

        $block = new Block('hash', 'prevhash', 1, 12345);
        $tx = new Tx($block, 'txhash');

        $this->assertTableRowCount('txlogs_0000', 0);
        $tx->rollback();
        $this->assertTableRowCount('txlogs_0000', 1);
        $row = Txlogs::first();
        $this->assertEquals('txhash', $row['tx_hash']);
        $this->assertEquals(Txlogs::ROW_TYPE_ROLLBACK, $row['handle_type']);

        $this->tableDeleteLike('txlogs_%');
    }
}