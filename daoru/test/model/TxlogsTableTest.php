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

    public function testGetCurrentTableWithoutCache() {
        // 新建表
        $this->assertFalse($this->tableExists('txlogs_0000'));
        $this->assertFalse($this->tableExists('txlogs_0001'));
        $table = Txlogs::getCurrentTable(false);
        $this->assertEquals('txlogs_0000', $table);
        $this->assertTrue($this->tableExists('txlogs_0000'));
        $this->assertFalse($this->tableExists('txlogs_0001'));

        // 数据没有达到阈值，不切换到下一张表
        $table = Txlogs::getCurrentTable(false);
        $this->assertEquals('txlogs_0000', $table);
        $this->assertTrue($this->tableExists('txlogs_0000'));
        $this->assertFalse($this->tableExists('txlogs_0001'));

        // 数据达到阈值，切换表
        $tmp = Config::get('app.txlogs_maximum_rows');      //临时设置为 0
        Config::put('app.txlogs_maximum_rows', 0);

        $table = Txlogs::getCurrentTable(false);
        $this->assertEquals('txlogs_0001', $table);
        $this->assertTrue($this->tableExists('txlogs_0001'));
        $this->assertTrue($this->tableExists('txlogs_0001'));

        $table = Txlogs::getCurrentTable(false);
        $this->assertEquals('txlogs_0002', $table);
        $this->assertTrue($this->tableExists('txlogs_0001'));
        $this->assertTrue($this->tableExists('txlogs_0002'));

        Config::put('app.txlogs_maximum_rows', $tmp);      //还原
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