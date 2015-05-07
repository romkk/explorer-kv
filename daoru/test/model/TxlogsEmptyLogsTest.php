<?php

use Carbon\Carbon;

class TxlogsEmptyLogsTest extends ExplorerDatabaseTestCase {

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

    public function testEmptyLogs() {
        $logs = Txlogs::getTempLogs();
        $this->assertEquals(0, count($logs));
    }

    /**
     * Returns the test dataset.
     *
     * @return PHPUnit_Extensions_Database_DataSet_IDataSet
     */
    protected function getDataSet() {
        return new DbUnit_ArrayDataSet([
            'txlogs_0000' => []
        ]);

    }
}