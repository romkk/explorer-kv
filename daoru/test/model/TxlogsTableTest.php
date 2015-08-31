<?php

class TxlogsTableTest extends ExplorerDatabaseTestCase {

    protected $conn;

    protected function setUp() {
        $this->conn = $this->getPDO();
        parent::setUp();
    }

    protected function tearDown() {
        $this->tableDeleteLike('txlogs_%');
        parent::tearDown();
    }

    public function testCreateFirstTable() {
        $this->assertFalse($this->tableExists('txlogs_0000'));
        Txlogs::createNewTable(null);
        $this->assertTrue($this->tableExists('txlogs_0000'));
    }

    public function testCreateNextTable() {
        $this->conn->exec('create table txlogs_0000 like 0_tpl_txlogs;');
        $this->assertTrue($this->tableExists('txlogs_0000'));
        $this->assertFalse($this->tableExists('txlogs_0001'));
        $table = Txlogs::createNewTable('txlogs_0000');
        $this->assertTrue($this->tableExists('txlogs_0001'));
        $this->assertEquals('txlogs_0001', $table);
        $table = Txlogs::createNewTable('txlogs_0001');
        $this->assertTrue($this->tableExists('txlogs_0002'));
        $this->assertEquals('txlogs_0002', $table);
    }

    public function testGetEmptyTableList(){
        $this->assertEquals(0, count(Txlogs::getTableList()));
    }

    public function testGetTableList(){
        $this->tableCreateLike('txlogs_0000', '0_tpl_txlogs');
        $this->tableCreateLike('txlogs_0001', '0_tpl_txlogs');

        $list = Txlogs::getTableList();
        $this->assertEquals(2, count($list));
        $this->assertEquals('txlogs_0001', $list[0]);
        $this->assertEquals('txlogs_0000', $list[1]);
    }

    /**
     * Returns the test dataset.
     *
     * @return PHPUnit_Extensions_Database_DataSet_IDataSet
     */
    protected function getDataSet() {
        return new DbUnit_ArrayDataSet([]);
    }
}