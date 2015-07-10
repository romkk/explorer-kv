require('babel/register')({
    optional: [
        'es7.asyncFunctions'
    ]
});

require('./bootstrap');
require('./bin/cli');