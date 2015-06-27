<?php
require __DIR__ . '/vendor/autoload.php';

use GuzzleHttp\Client;

date_default_timezone_set('UTC');

# load dotenv
Dotenv::load(__DIR__);

# bootstrap the app
App::init();

# path
Config::init(require __DIR__ . '/bootstrap/paths.php');

# monolog
Log::init();

# database
require __DIR__ . '/bootstrap/database.php';

# util
function reportStatus($value = null) {
    $url = Config::get('app.monitor_endpoint');
    $service = Config::get('app.monitor_service_name');

    $STDERR = fopen('php://stderr', 'w+');

    Log::info('reportStatus: 开始汇报状态');

    if (is_null($url) || is_null($service)) {
        $msg = 'reportStatus: Monitor 配置无效';
        Log::warning($msg);
        fwrite($STDERR, "$msg\n");
    }

    Log::info(sprintf('service = %s, value = %s', $service, $value));

    try {
        $client = new Client();
        $client->get($url, [
            'query' => [
                'service' => $service,
                'value' => $value
            ],
            'timeout' => 3,
            'connect_timeout' => 3,
            'version' => 1.0        //set http version to 1.0 to close the tcp connection !!!
        ]);
        Log::info('reportStatus: success');
    } catch (GuzzleHttp\Exception\TransferException $e) {
        Log::warning(sprintf('reportStatus: report to monitor failed', [
            $e->getCode(),
            $e->getMessage(),
        ]));
        fwrite($STDERR, "reportStatus: report to monitor failed\n");
    }

    fclose($STDERR);
}