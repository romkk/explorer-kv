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

function usage() {
    global $argv;
    echo sprintf('[usage] %s $startIndex $endIndex

files will be dump to new directory in %s named by start_index and end_index.
', $argv[0], __DIR__);
    exit(0);
}

//helper
function ensureDirectory($startIndex, $endIndex) {
    $dirname = $startIndex.'_'.$endIndex;
    $fullpath = __DIR__ . '/' . $dirname;
    if (file_exists($fullpath)) {
        echo sprintf('文件夹 %s 已存在，请检查。', $fullpath);
        exit(1);
    }

    if (!mkdir($fullpath)) {
        echo sprintf('创建文件夹 %s 失败，请检查。', $fullpath);
        exit(1);
    }

    return $fullpath;
}

function format(array $detail) {
    $ret = [];

    extract($detail);
    $now = date('Y-m-d H:i:s');

    // rawblocks
    $ret['rawBlocks'] = join(',', [$height + 1, $hash, $height, 0, $rawhex, $now]) . "\n";

    // rawtxs
    $ret['rawtxs'] = array_map(function($t) use ($detail, $now) {
        return [$t['hash'], join(',', ['{}', $t['hash'], $t['rawhex'], $now]) . "\n"];  // {}: placeholder
    }, $tx);

    return $ret;
}

function getRawTxsIndex($hash) {
    return hexdec(substr($hash, -2)) % 64;
}


//begin------------------------------------------------------------------

if ($argc != 3) {
    usage();
}

$startIndex = $argv[1];
$endIndex = $argv[2];

$fullpath = ensureDirectory($startIndex, $endIndex);
chdir($fullpath);

$bitcoinClient = Bitcoin::make();

// initialize counters and files
$rawBlocksFile = fopen('0_raw_blocks', 'a');
//$txlogsTableIndex = 0;
//$txlogsFile = fopen('txlogs_0000', 'a');
//$txlogsCounter = 0;
$rawTxs = [];

for ($i = 0; $i < 64; $i++) {
    $table = sprintf('raw_txs_%04d.raw', $i);
    $rawTxs[$i] = fopen($table, 'a');
}

for ($i = $startIndex; $i <= $endIndex; $i++) {

    Log::info('processing block ' . $i);

    $detail = $bitcoinClient->bm_get_block_detail(strval($i));

    if (is_null($detail)) {
        $i--;
        continue;
    }

    $lines = format($detail);

    fwrite($rawBlocksFile, $lines['rawBlocks']);

    // rawtxs
    foreach ($lines['rawtxs'] as $tx) {
        $hash = $tx[0];
        $line = $tx[1];

        //硬编码需要跳过的 tx
        if ($i == 91842 && $hash == 'd5d27987d2a3dfc724e359870c6644b40e497bdc0589a033220fe15429d88599' ||
            $i == 91880 && $hash == 'e3bf3d07d4b0375638d5f1db5255fe07ba2c4cb067cd81b84ee974b6585fb468') {
            continue;
        }

        fwrite($rawTxs[getRawTxsIndex($hash)], $line);
    }
}

//close all file descriptor
fclose($rawBlocksFile);
//fclose($txlogsFile);
for ($i = 0; $i < 64; $i++) {
    fclose($rawTxs[$i]);
}
