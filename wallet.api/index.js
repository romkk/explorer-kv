require('babel/register')({
    optional: [
        'es7.asyncFunctions'
    ]
});

require('dotenv').load();

// 开发时使用 bluebird，便于定位错误
global.Promise = require('bluebird');

require('./bin/cli');