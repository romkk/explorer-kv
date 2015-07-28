var mysql = require('mysql');
var log = require('debug')('wallet:mysql');
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

    query(sql, vars = [], conn = this.pool) {
        log(`SQL: ${sql}, bindings: ${vars}`);
        let start = Date.now();
        return new Promise((resolve, reject) => {
            conn.query(sql, vars, (err, rows) => {
                if (err) {
                    return reject(this.handleError(err));
                }
                log(`SQL: total ms: ${Date.now() - start}ms`);
                resolve(rows);
            });
        });
    }

    selectOne(sql, vars = [], conn = this.pool) {
        return this.query(sql, vars, conn)
            .then(rows => {
                return rows && rows.length ? rows[0] : null;
            });
    }

    list(sql, key, vars = [], conn = this.pool) {
        return this.query(sql, vars, conn)
            .then(rows => {
                return rows.map(r => r[key]);
            });
    }

    pluck(sql, key, vars = [], conn = this.pool) {
        return this.selectOne(sql, vars, conn)
            .then(row => {
                return row ? row[key] : null;
            });
    }

    async transaction(cb) {
        let c;

        let rollback = err => {
            // 回滚失败无需处理，mysql 会自行处理
            log(`rollback, message = ${err.message}`);
            c.conn.rollback(err => {
                if (err) {
                    log(`[ERROR] rollback error, message = ${err.message}`);
                }
                log(`rollback success`);
            });
        };

        try {
            c = await new Promise((resolve, reject) => {
                this.pool.getConnection((err, conn) => {
                    if (err) return reject(err);
                    conn.beginTransaction(err => {
                        if (err) return reject(err);
                        var c = {
                            conn: conn
                        };
                        ['query', 'pluck', 'list', 'selectOne'].forEach(method => {
                            c[method] = (...args) => {
                                args.push(conn);
                                return module.exports[method].apply(module.exports, args);
                            };
                        });
                        c.release = () => conn.release.call(conn);
                        resolve(c);
                    });
                });
            });
        } catch (err) {
            log(`开启事务失败 message = ${err.message}`);
            throw err;
        }

        // execute
        try {
            await cb(c);
            c.release();
        } catch (err) { // rollback
            rollback(err);
            throw err;
        }

        // commit
        try {
            await new Promise((resolve, reject) => {
                c.conn.commit(err => {
                    if (err) {
                        rollback(err);
                        return reject(err);
                    }
                    resolve();
                });
            });
        } catch (err) { // rollback
            rollback(err);
            throw err;
        }
    }
}

module.exports = Mysql.make();