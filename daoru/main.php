<?php
require __DIR__ . '/bootstrap.php';

//demo

// 1. clear temp log
Txlogs::clearTempLogs(Txlogs::getTempLogs());
// 2. compare block height and block hash of local and remote
