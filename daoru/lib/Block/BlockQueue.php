<?php

class BlockQueue {

    public static function getLocalBlockInfo() {
        $i = RawBlock::findLatestBlock();

        if (is_null($i)) {
            return [
                'block_height' => -1,
                'block_hash' => null
            ];
        }

        return $i->toArray();
    }

    public static function getRemoteBlockInfo() {
        $i = Bitcoin::make()->bm_get_best_block();
        return [
            'block_height' => $i['height'],
            'block_hash' => $i['hash'],
        ];
    }
}