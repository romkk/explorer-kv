var mysql = require('../../lib/mysql');

describe('mysql', ()=> {
    it('connect to server', done => {
        mysql.pool.getConnection((err, conn) => {
            expect(err).be.null;
            conn.ping(err => {
                expect(err).be.null;
                done();
            });
        });
    });

    it('execute query successfully', done => {
        mysql.query('select 1 + 1 as cnt')
            .then(rows => {
                expect(rows.length).be.eq(1);
                expect(rows[0].cnt).be.eq(2);
                done();
            });
    });

    it('execute query failed', done => {
        mysql.query('fatal')
            .catch(err => {
                expect(err).be.an.instanceOf(Error);
                done();
            });
    });

    it('execute query get emtpy', ()=> {
        return expect(mysql.query('show tables like "non-exists"'))
            .eventually.be.empty;
    });

    it('selectOne with one row', done => {
        mysql.selectOne('select 2 + 2 as cnt')
            .then(row => {
                expect(row.cnt).be.eq(4);
                done();
            });
    });

    it('selectOne with multi rows', done => {
        mysql.selectOne('select 2 + 2 as cnt union all select 3 + 3 as cnt')
            .then(row => {
                expect(row.cnt).be.eq(4);
                done();
            });
    });

    it('selectOne with null', () => {
        return expect(mysql.selectOne('show tables like "non-exits"'))
            .eventually.be.null;
    });

    it('list', () => {
        return expect(mysql.list('select 1 + 1 as cnt union all select 2 + 2 as cnt', 'cnt'))
            .eventually.be.deep.eq([2, 4]);
    });

    it('list get empty', () => {
        return expect(mysql.list('show tables like "non-exists"', 'key'))
            .eventually.be.empty;
    });

    it('pluck', () => {
        return expect(mysql.pluck('select 1 + 1 as cnt union all select 2 + 2 as cnt', 'cnt'))
            .eventually.be.eq(2);
    });

    it('pluck get null', () => {
        return expect(mysql.pluck('show tables like "non-exists"', 'key'))
            .eventually.be.null;
    });
});