var restify = require('restify');
var mysql = require('../lib/mysql');
var sprintf = require('sprintf').sprintf;
var log = require('debug')('api:server');
var bunyan = require('bunyan');
var expressValidator = require('express-validator');
var customValidators = require('../lib/custom_validators');

var server = restify.createServer();

server.pre(restify.pre.userAgentConnection());
server.pre(restify.pre.sanitizePath());
server.use(restify.acceptParser(server.acceptable));
server.use(restify.CORS());
server.use(restify.queryParser());
server.use(restify.jsonp());
server.use(restify.bodyParser());
server.use(expressValidator({ customValidators: customValidators }));
server.use(restify.gzipResponse());

server.on('after', restify.auditLogger({
    log: bunyan.createLogger({
        name: 'Audit',
        stream: process.stdout
    })
}));

require('../route')(server);

server.listen(3000, ()=> {
    log('listen on 3000');
});

server.on('uncaughtException', function(req, res, route, err) {
    throw err;
});