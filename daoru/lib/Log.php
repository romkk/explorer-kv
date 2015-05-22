<?php

use Monolog\Formatter\LineFormatter;
use Monolog\Handler\AbstractHandler;
use Monolog\Logger;

class Log {
    private static $logger;

    public static function init() {
        self::$logger = new Logger('daoru');

        $filename = getenv('ENV') === 'test' ? 'daoru.test.log' : 'daoru.log';
        $handler = new Monolog\Handler\RotatingFileHandler(Config::get('app.log_path') . '/' . $filename, 3);
        $handler->setFormatter(new LineFormatter(null, 'Y-m-d H:i:s.u'));
        self::$logger->pushHandler($handler);
    }

    public static function __callStatic($name, $args) {
        if (in_array($name, ['debug', 'info', 'notice', 'warning', 'error', 'critical', 'alert', 'emergency'])) {
            $method = 'add' . ucfirst($name);

            if (is_string($args[0]) && ($stacktrace = debug_backtrace()) && isset($stacktrace[2])) {
                $stack = $stacktrace[2];
                $prefix = [];
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