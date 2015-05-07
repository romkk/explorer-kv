<?php

use Carbon\Carbon;

class TxlogsSimpleTempLogsTest extends ExplorerDatabaseTestCase {

    protected $conn;

    protected function setUp() {
        $this->conn = $this->getPDO();
        $this->tableCreateLike('txlogs_0000', '0_tpl_txlogs');
        parent::setUp();
    }

    protected function tearDown() {
        $this->tableDeleteLike('txlogs_%');
        PHPUnit_Extensions_Database_Operation_Factory::DELETE_ALL();
        parent::tearDown();
    }

    public function testFindAndClearTempLogsWithOnePage() {
        $logs = Txlogs::getTempLogs();
        $this->assertEquals(2, count($logs));
        //测试顺序是否正确
        $this->assertEquals('txhash7', $logs[0]['tx_hash']);
        $this->assertEquals('txhash6', $logs[1]['tx_hash']);

        Txlogs::clearTempLogs($logs);
        $this->assertEquals(9, $this->getConnection()->getRowCount('txlogs_0000'));
        $this->assertEquals(0, count(Txlogs::getTempLogs()));

        $rows = Txlogs::orderBy('id', 'desc')->get();
        $this->assertEquals('txhash6', $rows[0]->tx_hash);
        $this->assertEquals('txhash7', $rows[1]->tx_hash);
        $this->assertEquals('txhash7', $rows[2]->tx_hash);
        $this->assertEquals(2, $rows[0]->handle_type);
        $this->assertEquals(2, $rows[1]->handle_type);
        $this->assertEquals(1, $rows[2]->handle_type);
    }

    public function testFindAndClearTempLogsWithMultiPage() {
        $logs = Txlogs::getTempLogs(2);

        $this->assertEquals(2, count($logs));
        //测试顺序是否正确
        $this->assertEquals('txhash7', $logs[0]['tx_hash']);
        $this->assertEquals('txhash6', $logs[1]['tx_hash']);

        Txlogs::clearTempLogs($logs);
        $this->assertEquals(9, $this->getConnection()->getRowCount('txlogs_0000'));
        $this->assertEquals(0, count(Txlogs::getTempLogs()));

        $rows = Txlogs::orderBy('id', 'desc')->get();
        $this->assertEquals('txhash6', $rows[0]->tx_hash);
        $this->assertEquals('txhash7', $rows[1]->tx_hash);
        $this->assertEquals('txhash7', $rows[2]->tx_hash);
        $this->assertEquals(2, $rows[0]->handle_type);
        $this->assertEquals(2, $rows[1]->handle_type);
        $this->assertEquals(1, $rows[2]->handle_type);
    }

    /**
     * Returns the test dataset.
     *
     * @return PHPUnit_Extensions_Database_DataSet_IDataSet
     */
    protected function getDataSet() {
        return new DbUnit_ArrayDataSet([
            'txlogs_0000' => [
                ['id' => 1, 'handle_status' => 100, 'handle_type' => 1, 'block_height' => 0, 'tx_hash' => 'txhash1', 'created_at' => Carbon::now(), 'updated_at' => Carbon::now()],
                ['id' => 2, 'handle_status' => 100, 'handle_type' => 1, 'block_height' => 1, 'tx_hash' => 'txhash2', 'created_at' => Carbon::now(), 'updated_at' => Carbon::now()],
                ['id' => 3, 'handle_status' => 100, 'handle_type' => 2, 'block_height' => 1, 'tx_hash' => 'txhash3', 'created_at' => Carbon::now(), 'updated_at' => Carbon::now()],
                ['id' => 4, 'handle_status' => 100, 'handle_type' => 1, 'block_height' => -1, 'tx_hash' => 'txhash4', 'created_at' => Carbon::now(), 'updated_at' => Carbon::now()],
                ['id' => 5, 'handle_status' => 100, 'handle_type' => 2, 'block_height' => -1, 'tx_hash' => 'txhash5', 'created_at' => Carbon::now(), 'updated_at' => Carbon::now()],
                ['id' => 6, 'handle_status' => 100, 'handle_type' => 1, 'block_height' => -1, 'tx_hash' => 'txhash6', 'created_at' => Carbon::now(), 'updated_at' => Carbon::now()],
                ['id' => 7, 'handle_status' => 100, 'handle_type' => 1, 'block_height' => -1, 'tx_hash' => 'txhash7', 'created_at' => Carbon::now(), 'updated_at' => Carbon::now()],
            ]
        ]);

    }
}