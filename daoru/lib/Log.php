<?php

use Monolog\Logger;
use Monolog\Handler\RotatingFileHandler;

class Log {
    private static $logger;

    public static function init() {
        self::$logger = new Logger('daoru');
        self::$logger->pushHandler(new RotatingFileHandler(Config::get('app.log_path') . '/daoru.log', 3));
    }

    public static function __callStatic($name, $args) {
        if (in_array($name, ['debug', 'info', 'notice', 'warning', 'error', 'critical', 'alert', 'emergency'])) {
            $method = 'add' . ucfirst($name);
            call_user_func_array([self::$logger, $method], func_get_args());
        }
    }
}