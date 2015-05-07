<?php
return [
    'host' => getenv('DATABASE_HOST'),
    'name' => getenv('BitcoinExplorerDB'),
    'user' => getenv('DATABASE_USER'),
    'pass' => getenv('DATABASE_PASS'),
    'port' => getenv('DATABASE_PORT'),
    'log' => true
];