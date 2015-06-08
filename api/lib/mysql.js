var mysql = require('mysql');
var log = require('debug')('api:mysql');
var config = require('config');

class Mysql {

    static instance;

    static make() {
        if (Mysql.instance) {
            return Mysql.instance;
        }
        var pool = mysql.createPool({
            host: config.get('database.host'),
            database: config.get('database.name'),
            port: config.get('database.port'),
            user: config.get('database.user'),
            password: config.get('database.pass'),
            connectionLimit: 50,
            charset: 'utf8',
            timezone: 'UTC',
            multipleStatements: true,
            waitForConnections: true,
            queueLimit: 0,
            acquireTimeout: 30000,
            debug: false
        });

        pool.on('connection', ()=> {
            log('New mysql connection created');
        });

        pool.on('enqueue', ()=> {
            log('Waiting for available conneciton slot');
        });

        Mysql.instance = new Mysql(pool);
        Mysql.instance.debug = process.env.ENV != 'prod';

        return Mysql.instance;
    }

    constructor(pool) {
        this.pool = pool;
    }

    handleError(err) {
        log('SQL 错误', err);
        return err;
    }

    query(sql, vars = []) {
        log(`SQL: ${sql}, bindings: ${vars}`);
        let start = Date.now();
        return new Promise((resolve, reject) => {
            this.pool.query(sql, vars, (err, rows) => {
                if (err) {
                    return reject(this.handleError(err));
                }
                log(`SQL: total ms: ${Date.now() - start}ms`);
                resolve(rows);
            });
        });
    }

    selectOne(sql, vars = []) {
        return this.query(sql, vars)
            .then(rows => {
                return rows && rows.length ? rows[0] : null;
            });
    }

    list(sql, key, vars = []) {
        return this.query(sql, vars)
            .then(rows => {
                return rows.map(r => r[key]);
            });
    }

    pluck(sql, key, vars = []) {
        return this.selectOne(sql, vars)
            .then(row => {
                return row ? row[key] : null;
            });
    }
}

module.exports = Mysql.make();