#!/usr/bin/env php
<?php
//bootstrap things..
require __DIR__ . '/../vendor/autoload.php';

date_default_timezone_set('UTC');

# load dotenv
Dotenv::load(__DIR__);
Config::init(require __DIR__ . '/../bootstrap/paths.php');
App::init();
Log::init();

require __DIR__ . '/../bootstrap/database.php';


// update placeholder in raw_txs_%04d

$index = [];
for ($i = 0; $i < 64; $i++) {
    $index[$i] = RawTx::getNextId(sprintf('raw_txs_%04d', $i));
}

$dirs = glob('[0-9]*_[0-9]*', GLOB_ONLYDIR);
usort($dirs, function($a, $b) {
    $fmt = '%d_%d';
    sscanf($a, $fmt, $ia, $_);
    sscanf($b, $fmt, $ib, $_);
    return $ia - $ib;
});

foreach ($dirs as $d) {
    foreach (glob($d.'/raw_txs_*.raw') as $f) {
        $rfd = fopen($f, 'r');
        $wfd = fopen($d.'/'.basename($f, '.raw'), 'a');

        $tablePostfix = sscanf($f, $d.'/raw_txs_%04d.raw', $id);

        while (($line = fgets($rfd)) !== false) {
            if (!array_key_exists($id, $index)) {
                dd($index, $id);
            }
            fwrite($wfd, str_replace('{}', $index[$id]++, $line));
        }

        fclose($rfd);
        fclose($wfd);

        // delete raw file
        unlink($f);
    }
}