 <?php

class Bitcoin {
    // Configuration options
    private $username;
    private $password;
    private $host;
    private $port;
    private $url;
    private $id = 0;

    private static $instance = null;

    public static function make() {
        if (is_null(self::$instance)) {
            self::$instance = new Bitcoin(
                Config::get('bitcoind.user'),
                Config::get('bitcoind.pass'),
                Config::get('bitcoind.host'),
                Config::get('bitcoind.port')
            );
        }

        return self::$instance;
    }

    function __construct($username, $password, $host = 'localhost', $port = 8332, $url = null) {
        $this->username      = $username;
        $this->password      = $password;
        $this->host          = $host;
        $this->port          = $port;
        $this->url           = $url;
    }

    function __call($method, $params) {
        $httpStatus = $rawResponse = $response = $curlError = null;

        // The ID should be unique for each call
        $this->id++;

        // Build the request, it's ok that params might have any empty array
        $request = json_encode([
            'id'     => $this->id,
            'method' => $method,
            'params' => $params,
        ]);

//        Log::debug('Bitcoind POST BODY', [$request]);

        // Build the cURL session
        $url = "http://{$this->username}:{$this->password}@{$this->host}:{$this->port}/{$this->url}";
        $curl = curl_init($url);
        $options = array(
            CURLOPT_RETURNTRANSFER => TRUE,
            CURLOPT_FOLLOWLOCATION => TRUE,
            CURLOPT_MAXREDIRS      => 10,
            CURLOPT_HTTPHEADER     => array('Content-type: application/json'),
            CURLOPT_POST           => TRUE,
            CURLOPT_POSTFIELDS     => $request,
            CURLOPT_TIMEOUT        => 60,
        );

        $options[CURLOPT_VERBOSE] = getenv('ENV' === 'local');

        curl_setopt_array($curl, $options);

        // Execute the request and decode to an array
        $rawResponse = curl_exec($curl);
        $statusCode = curl_getinfo($curl, CURLINFO_HTTP_CODE);
        $response = json_decode($rawResponse, true);
        $curlError = curl_error($curl);
        curl_close($curl);

//        Log::debug('Bitconid response', [
//            'rawResponse' => $rawResponse,
//            'statusCode' => $statusCode,
//            'curlError' => $curlError,
//        ]);

        if ($statusCode >= 400 && $statusCode < 500) {
            throw new Exception('与 Bitcoind 连接失败', $statusCode);
        }

        if (isset($response['error'])) {
            throw new BitcoindException($response['error']['message'], $statusCode, $curlError, $rawResponse, $url);
        }

        return $response['result'];
    }

    public function getBlockByHeight($height) {
        $hash = $this->getblockhash($height);
        return $this->bm_get_block_detail($hash);
    }
}

class BitcoindException extends Exception {
    public $httpStatus;
    public $curlError;
    public $rawResponse;
    public $url;

    public function __construct($responseError, $httpStatus, $curlError, $rawResponse, $url) {
        parent::__construct('Bitcoind 请求发生错误: ' . $responseError);

        $this->httpStatus = $httpStatus;
        $this->curlError = $curlError;
        $this->rawResponse = $rawResponse;
        $this->url = $url;
        $this->code = $httpStatus;
    }
}