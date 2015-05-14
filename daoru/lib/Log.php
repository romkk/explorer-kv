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

            if (is_string($args[0])) {
                $prefix = [];
                $stack = debug_backtrace()[2];
                if (array_key_exists('class', $stack)) {
                    $prefix[] = $stack['class'];
                }

                if (array_key_exists('function', $stack)) {
                    $prefix[] = $stack['function'];
                }

                $args[0] = join('::', $prefix) . ' ->  ' . $args[0];
            }

            call_user_func_array([self::$logger, $method], $args);
        }
    }
}