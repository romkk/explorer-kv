<?php

use Carbon\Carbon;
use Illuminate\Support\Collection;

class BlockTest extends ExplorerDatabaseTestCase {

    /**
     * Returns the test dataset.
     *
     * @return PHPUnit_Extensions_Database_DataSet_IDataSet
     */
    protected function getDataSet() {
        return new DbUnit_ArrayDataSet([]);
    }

    public function testGetHex() {
        $this->tableInsert('0_raw_blocks', [
            ['id' => 1, 'block_hash' => 'block_hash', 'block_height' => 0, 'chain_id' => 0, 'hex' => 'hex', 'created_at' => Carbon::now()->toDateTimeString()],
        ]);

        $block = new Block('block_hash', 'prev_hash', 0, 1);
        $this->assertEquals('hex', $block->getHex());

        $block = new Block('block_hash', 'prev_hash', 0, 1);
        $block->setHex('hex2');
        $this->assertEquals('hex2', $block->getHex());

        $this->tableTruncate('0_raw_blocks');
    }

    public function testGetTxs() {
        $this->tableInsert('0_raw_blocks', [
            ['id' => 1, 'block_hash' => 'block_hash', 'block_height' => 0, 'chain_id' => 0, 'hex' => 'hex', 'created_at' => Carbon::now()->toDateTimeString()],
        ]);

        $block = new Block('block_hash', 'prev_hash', 0, 1);
        $block->setTxs([]);
        $this->assertEquals([], $block->getTxs());

        $block = new Block(
            '00000000b4061b7d09fa7e2f1c9ada6f674c22ba7a0eac6d158b808925c0c766',
            '0000000051b02474c2c68fbbeda6ffd64e00ba518465104605b132e10ee8a266',
            394772,
            1
        );
        $txs = $block->getTxs();
        $this->assertEquals(1, count($txs));
        $this->assertEquals('d7cd58480b6e0363eafb32e4ac778ffeaafd144cb690a2756bf527d78ff49c97', $txs[0]->getHash());

        $this->tableTruncate('0_raw_blocks');
    }

    public function testCreateFromBlockDetail() {
        $detail = [
            'hash' => 'hash',
            'previousblockhash' => 'previousblockhash',
            'height' => 0,
            'time' => 1,
            'rawhex' => 'raw',
            'tx' => []
        ];
        $block = Block::createFromBlockDetail($detail);
        $this->assertInstanceOf('Block', $block);
        $this->assertEquals(0, $block->getHeight());
        $this->assertEquals('hash', $block->getHash());
        $this->assertEquals('previousblockhash', $block->getPrevHash());
    }

    public function testRollback() {
        $this->tableCreateLike('txlogs_0000', '0_tpl_txlogs');

        $block = Block::createFromBlockDetail(Bitcoin::make()->bm_get_block_detail('000000000000226f7618566e70a2b5e020e29579b46743f05348427239bf41a1'))->setId(1);        // height = 300000
        $this->assertEquals('00000000dfe970844d1bf983d0745f709368b5c66224837a17ed633f0dabd300', $block->getPrevHash());
        $this->assertEquals(2, count($block->getTxs()));

        $block->rollback();

        $rows = Txlogs::orderBy('id')->get();
        $this->assertEquals(2, count($rows));
        $this->assertEquals('caa307764f31e52def1368847b7418c6683feaad00a9752405c2a9f99ab2154b', $rows[0]->tx_hash);
        $this->assertEquals(2, $rows[0]->handle_type);
        $this->assertEquals('9e109b6b9ca374d573f9e923490076980d612d7ea75c62b6209b2001ff36f38c', $rows[1]->tx_hash);
        $this->assertEquals(2, $rows[1]->handle_type);

        $this->tableDeleteLike('txlogs_%');
    }
}