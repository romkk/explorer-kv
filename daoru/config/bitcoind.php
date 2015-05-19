<?php
return [
    'host' => getenv('BITCOIND_HOST'),
    'user' => getenv('BITCOIND_USER'),
    'pass' => getenv('BITCOIND_PASS'),
    'port' => getenv('BITCOIND_PORT'),
    'debug' => false,
];