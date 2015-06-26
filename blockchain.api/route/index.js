module.exports = function(server) {
    require('./latestblock')(server);
    require('./block')(server);
    require('./tx')(server);
    require('./address')(server);
    require('./misc')(server);
    require('./ping')(server);
};

