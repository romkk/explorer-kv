<?php

use Carbon\Carbon;

class RawTxTest extends ExplorerDatabaseTestCase {

    public function testGetTableByHash() {
        $this->assertEquals('raw_txs_0023', RawTx::getTableByHash('d7cd58480b6e0363eafb32e4ac778ffeaafd144cb690a2756bf527d78ff49c97'));
    }

    public function testGetTableById() {
        $this->assertEquals('raw_txs_0000', RawTx::getTableById(123));
        $this->assertEquals('raw_txs_0010', RawTx::getTableById(100e8));
        $this->assertEquals('raw_txs_0063', RawTx::getTableById(63012345678));
    }

    public function testSave() {
        $this->assertTableRowCount('raw_txs_0023', 0);
        $tx = new RawTx([
            'tx_hash' => 'd7cd58480b6e0363eafb32e4ac778ffeaafd144cb690a2756bf527d78ff49c97',
            'hex' => 'hex',
            'created_at' => Carbon::now()->toDateTimeString(),
        ]);
        $tx->save();
        $this->assertEquals(230e8, $tx->id);
        $this->assertTableRowCount('raw_txs_0023', 1);
    }

    public function testGetNextId() {
        $id = RawTx::getNextId('raw_txs_0023');
        $this->assertEquals(230e8, $id);

        $this->tableInsert('raw_txs_0023', [
            ['id' => 23000000220, 'tx_hash' => 'hash', 'hex' => 'hex', 'created_at' => Carbon::now()->toDateTimeString()]
        ]);

        $id = RawTx::getNextId('raw_txs_0023');
        $this->assertEquals(23000000221, $id);

        $this->tableInsert('raw_txs_0023', [
            ['id' => 23000000221, 'tx_hash' => 'hash2', 'hex' => 'hex', 'created_at' => Carbon::now()->toDateTimeString()]
        ]);

        $id = RawTx::getNextId('raw_txs_0023');
        $this->assertEquals(23000000222, $id);
    }

    public function testTxExists() {
        $this->assertFalse(Rawtx::txExists('non-exists'));

        $this->tableInsert('raw_txs_0023', [
            ['id' => 23000000221, 'tx_hash' => 'hash17', 'hex' => 'hex', 'created_at' => Carbon::now()->toDateTimeString()]
        ]);

        $this->assertTrue(RawTx::txExists('hash17'));
    }

    /**
     * Returns the test dataset.
     *
     * @return PHPUnit_Extensions_Database_DataSet_IDataSet
     */
    protected function getDataSet() {
        return new DbUnit_ArrayDataSet([
            'raw_txs_0000' => [],
            'raw_txs_0023' => [],
            '0_explorer_meta' => [],
        ]);
    }
}