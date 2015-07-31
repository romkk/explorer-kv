//let xg = require('../lib/xg');

module.exports = server => {
    require('./misc')(server);
    require('./tx')(server);
    require('./auth')(server);
    require('./device')(server);
    require('./multisig')(server);
    require('./ping')(server);
    require('./sts')(server);
    require('./bm_account')(server);

    //server.get('/test', async (req, res, next) => {
    //    xg.send(
    //        'w_43012e84a441a255970fd99b5d90acb0ec1cadee77362f961150433dd0ff8059',
    //        xg.EVENT_MULTISIG_ACCOUNT_CHANGE,
    //        ['k', 'v']
    //    );
    //    res.send(200);
    //    return next();
    //});
};