var mysql = require('mysql');
var log = require('debug')('api:mysql');

var pool = mysql.createPool({
    connectionLimit: 20,
    host: process.env.DATABASE_HOST,
    database: process.env.DATABASE_NAME,
    port: process.env.DATABASE_PORT,
    user: process.env.DATABASE_USER,
    password: process.env.DATABASE_PASS,
    charset: 'utf8',
    timezone: 'UTC',
    multipleStatements: true,
    acquireTimeout: 3000,
    waitForConnections: true,
    queueLimit: 5
});

pool.on('connection', ()=> {
    log('New mysql connection created');
});

pool.on('enqueue', ()=> {
    log('Waiting for available conneciton slot');
});

module.exports = pool;