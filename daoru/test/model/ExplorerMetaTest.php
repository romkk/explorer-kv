<?php

use Carbon\Carbon;

class ExplorerMetaTest extends ExplorerDatabaseTestCase {

    public function testGetPutData() {
        $this->assertNull(ExplorerMeta::get('non-exists'));
        $this->assertEquals('default', ExplorerMeta::get('non-exitst', 'default'));
        $this->assertTableRowCount('0_explorer_meta', 0);

        ExplorerMeta::put('k', 'v');
        $this->assertTableRowCount('0_explorer_meta', 1);
        $this->assertEquals('v', ExplorerMeta::get('k'));

        ExplorerMeta::put('k', 'v2');
        $this->assertTableRowCount('0_explorer_meta', 1);
        $this->assertEquals('v2', ExplorerMeta::get('k'));

        ExplorerMeta::put('k2', 'v2');
        $this->assertTableRowCount('0_explorer_meta', 2);
        $this->assertEquals('v2', ExplorerMeta::get('k2'));
    }

    public function testGetCurrentId() {
        $this->assertEquals(0, RawTx::currentId());

        $this->tableInsert('0_explorer_meta', [
            [
                'id' => 1,
                'key' => 'raw_txs_id',
                'value' => '100',
                'created_at' => Carbon::now()->toDateTimeString(),
                'updated_at' => Carbon::now()->toDateTimeString(),
            ]
        ]);

        $this->assertEquals(100, RawTx::currentId());

        $this->tableTruncate('0_explorer_meta');
    }

    /**
     * Returns the test dataset.
     *
     * @return PHPUnit_Extensions_Database_DataSet_IDataSet
     */
    protected function getDataSet() {
        return new DbUnit_ArrayDataSet([
            '0_explorer_meta' => [],
        ]);
    }
}