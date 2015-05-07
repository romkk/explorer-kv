<?php
require __DIR__ . '/../vendor/autoload.php';

Dotenv::load(__DIR__);

Config::init(require __DIR__ . '/../bootstrap/paths.php');

Log::init(new Monolog\Handler\RotatingFileHandler(Config::get('app.log_path') . '/daoru.log', 3));

require __DIR__ . '/../bootstrap/database.php';