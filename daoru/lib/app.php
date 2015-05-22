<?php

use Illuminate\Container\Container;

class App {
    public static $container = null;

    public static function init() {
        self::$container = new Container();
    }
}