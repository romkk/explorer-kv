module.exports = function(server) {
    require('./latestblock')(server);
    require('./block')(server);
    require('./tx')(server);
    require('./address')(server);
    require('./misc')(server);
    require('./bitcoind_tools')(server);
    require('./api')(server);
    require('./ping')(server);
};

