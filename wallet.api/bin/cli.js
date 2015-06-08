var restify = require('restify');
var sprintf = require('sprintf').sprintf;
var moment = require('moment');

var debug = require('debug');
debug.formatArgs = function () {
    var args = arguments;
    var useColors = this.useColors;
    var name = this.namespace;

    if (useColors) {
        var c = this.color;

        args[0] = '  \u001b[3' + c + ';1m' + name + ' '
            + '\u001b[0m'
            + args[0] + '\u001b[3' + c + 'm'
            + ' +' + debug.humanize(this.diff) + '\u001b[0m';
    } else {
        args[0] = moment.utc().format('YYYYMMDD.HHmmss.SSS')
            + ' ' + name + ' ' + args[0];
    }
    return args;
};


var log = debug('wallet:server');
var bunyan = require('bunyan');
var expressValidator = require('express-validator');

var server = restify.createServer();

server.pre(restify.pre.userAgentConnection());
server.pre(restify.pre.sanitizePath());
server.use(restify.CORS());
server.use(restify.acceptParser(server.acceptable));
server.use(restify.queryParser());
server.use(restify.jsonp());
server.use(restify.bodyParser());
server.use(expressValidator());
server.use(restify.gzipResponse());

server.on('after', restify.auditLogger({
    log: bunyan.createLogger({
        name: 'Audit',
        stream: process.stdout
    })
}));

server.use((req, res, next) => {
    log(`URL: ${req.url}`);
    req.checkParams('skipcache', 'should be boolean').optional().isBoolean();
    req.sanitize('skipcache').toBoolean(true);

    next();
});

require('../route')(server);

server.listen(process.env.WALLET_API_PORT || 3001, ()=> {
    log(`listen on ${process.env.WALLET_API_PORT || 3001}`);
});

server.on('uncaughtException', function(req, res, route, err) {
    throw err;
});