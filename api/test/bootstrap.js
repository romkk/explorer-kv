require('dotenv').load({ path: `${__dirname}/.env` });

var path = require('path');
var fs = require('fs');
var log = require('debug')('api:test:bootstrap');
var exec = require('child_process').exec;
var databaseCleaner = require('database-cleaner');
var chai = require('chai');
var mysql = require('mysql');
var config = require('config');

chai.use(require("chai-as-promised"));

global.conn = mysql.createConnection({
    host: config.get('database.host'),
    port: config.get('database.port'),
    user: config.get('database.user'),
    password: config.get('database.pass'),
    database: config.get('database.name'),
    multipleStatements: true,
    debug: true
});

global.expect = chai.expect;

global.loadSQL = function(subpath, db = null) {
    return new Promise(function(resolve, reject) {
        var fullpath = path.join(__dirname, subpath);

        if (!fs.existsSync(fullpath)) {
            return reject(new Error(`file not found: ${fullpath}`));
        }

        conn.query(fs.readFileSync(fullpath, {encoding: 'utf8'}), (err, result) => {
            log('import done');
            if (err) {
                log('初始化数据库失败', err);
                return reject(err);
            }

            log('数据库初始化完成', fullpath);

            resolve();
        });
    });
};

global.clearDB = function() {
    return new Promise(function(resolve, reject) {
        (new databaseCleaner('mysql')).clean(conn, function() {
            resolve();
        });
    });
};
