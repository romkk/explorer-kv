module.exports = function(server) {
    require('./latestblock')(server);
    require('./block')(server);
};

