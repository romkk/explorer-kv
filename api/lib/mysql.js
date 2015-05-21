var mysql = require('mysql');
var log = require('debug')('api:mysql');

class Mysql {

    static instance;

    static make() {
        if (Mysql.instance) {
            return Mysql.instance;
        }
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
            queueLimit: 5,
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
        return new Promise((resolve, reject) => {
            this.pool.query(sql, vars, (err, rows) => {
                if (err) {
                    return reject(this.handleError(err));
                }
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