<?php
use Symfony\Component\ClassLoader\MapClassLoader;

require __DIR__ . '/../vendor/autoload.php';

date_default_timezone_set('UTC');

# load dotenv
Dotenv::load(__DIR__);

# bootstrap the app
App::init();

# path
Config::init(require __DIR__ . '/../bootstrap/paths.php');

# monolog
Log::init();

# database
require __DIR__ . '/../bootstrap/database.php';

# autoloader
call_user_func(function() {
    $classMapping = [
        'DbUnit_ArrayDataSet' => __DIR__ . '/TestBase/DbUnit_ArrayDataSet.php',
        'ExplorerDatabaseTestCase' => __DIR__ . '/TestBase/ExplorerDatabaseTestCase.php',
    ];

    $loader = new MapClassLoader($classMapping);
    $loader->register();
});