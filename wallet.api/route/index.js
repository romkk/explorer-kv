var mysql = require('../lib/mysql');
module.exports = server => {
    require('./misc')(server);
    require('./tx')(server);
    require('./auth')(server);
    require('./device')(server);
    require('./multisig')(server);
    require('./ping')(server);
};