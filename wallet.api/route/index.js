module.exports = server => {
    require('./misc')(server);
    require('./tx')(server);
};