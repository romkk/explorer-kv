<?php

abstract class Block {
    public static function getLocalLatestBlock() {}
    public static function getRemoteLatestBlock() {}
    public function save() {}
    public function rollback() {}
}