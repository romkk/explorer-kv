<?php

use Carbon\Carbon;

class RawTxTest extends ExplorerDatabaseTestCase {

    public function testGetTableByHash() {
        $tx = new RawTx();
        $tx->id = 1;
        $tx->tx_hash = 'd7cd58480b6e0363eafb32e4ac778ffeaafd144cb690a2756bf527d78ff49c97';
        $tx->hex = 'hex';
        $tx->created_at = Carbon::now()->toDateTimeString();

        $this->assertEquals('raw_txs_0023', RawTx::getTableByHash($tx));
    }

    /**
     * Returns the test dataset.
     *
     * @return PHPUnit_Extensions_Database_DataSet_IDataSet
     */
    protected function getDataSet() {
        return new DbUnit_ArrayDataSet([
            'raw_txs_0000' => [],
        ]);
    }
}