<?php
require __DIR__ . '/vendor/autoload.php';

# load dotenv
Dotenv::load(__DIR__);

# bootstrap the app
App::init();

# path
Config::init(require __DIR__ . '/bootstrap/paths.php');

# monolog
Log::init(new Monolog\Handler\RotatingFileHandler(Config::get('app.log_path') . '/daoru.log', 3));

# database
require __DIR__ . '/bootstrap/database.php';