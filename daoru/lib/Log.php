<?php

use Monolog\Handler\AbstractHandler;
use Monolog\Logger;

class Log {
    private static $logger;

    public static function init(AbstractHandler $handler) {
        self::$logger = new Logger('daoru');
        self::$logger->pushHandler($handler);
    }

    public static function __callStatic($name, $args) {
        if (in_array($name, ['debug', 'info', 'notice', 'warning', 'error', 'critical', 'alert', 'emergency'])) {
            $method = 'add' . ucfirst($name);
            call_user_func_array([self::$logger, $method], func_get_args());
        }
    }
}