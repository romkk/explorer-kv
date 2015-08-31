<?php

class Config {
    public static $cache = [];

    public static function init($pathDefinition) {
        Config::put('path', $pathDefinition);
        $configPath = Config::get('path.config');
        if (file_exists($configPath) && is_dir($configPath) && is_readable($configPath)) {
            $files = scandir($configPath);
            foreach($files as $f) {
                if ($f === '.' || $f === '..') continue;
                Config::$cache = array_set(Config::$cache, basename($f, '.php'), require $configPath.'/'.$f);
            }
        }
    }

    public static function get($key, $default = null) {
        return array_get(Config::$cache, $key, $default);
    }

    public static function put($key, $value) {
        return array_set(Config::$cache, $key, $value);
    }

    public static function has($key) {
        return array_has(Config::$cache, $key);
    }
}