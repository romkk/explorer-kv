<?php
require __DIR__ . '/../vendor/autoload.php';
Dotenv::load(__DIR__ . '/..');
Config::init(require __DIR__ . '/../bootstrap/paths.php');
Log::init();
