module.exports = {
    database: {
        host: process.env.DATABASE_HOST,
        user: process.env.DATABASE_USER,
        pass: process.env.DATABASE_PASS,
        port: process.env.DATABASE_PORT,
        name: process.env.DATABASE_NAME
    },

    ssdb: {
        host: process.env.SSDB_HOST,
        port: process.env.SSDB_PORT
    },

    bitcoind: {
        host: process.env.BITCOIND_HOST,
        port: process.env.BITCOIND_PORT,
        user: process.env.BITCOIND_USER,
        pass: process.env.BITCOIND_PASS
    },

    explorerEndpoint: 'http://tchain.btc.com/api/v1/',
    tokenExpiredOffset: 10 * 86400,     //token 有效期
    oss: {
        bucketOwnerId: '1581108693131087',      // listBucket 获得
        bucket: 'wallet-user-data',
        rootAccessKeyId: process.env.OSS_ACCESSKEY_ID,
        rootAccessKeySecret: process.env.OSS_ACCESSKEY_SECRET,
        durationSeconds: 900    // 令牌有效期
    },
    xg: {       //信鸽推送
        android: {
            accessId: process.env.XG_ANDROID_ACCESSID,
            secretKey: process.env.XG_ANDROID_SECRETKEY
        },
        ios: {
            accessId: process.env.XG_IOS_ACCESSID,
            secretKey: process.env.XG_IOS_SECRETKEY
        }
    },
    userCenter: process.env.UC_ENDPOINT,
    qrCodeEndpoint: process.env.QRCODE_ENDPOINT,
    mailEndpoint: process.env.EMAIL_GATEWAY_ENDPOINT
};
