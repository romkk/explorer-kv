<?php

return [
    'log_path' => __DIR__ . '/../log',

    'txlogs_maximum_rows' => 1000e4,     //txlogs 单张表最大记录数：1000万
    'batch_insert' => 500,      // 批量插入记录时的最大值

    'monitor_endpoint' => 'http://monitor.bitmain.com/monitor/api/v1/message',
    'monitor_service_name' => getenv('MONITOR_SERVICE_NAME'),
];