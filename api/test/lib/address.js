var Address = require('../../lib/address');

describe('address', ()=> {
    before(() => {
        return clearDB().then(() => {
            return loadSQL('fixture/lib/address.sql');
        });
    });

    after(() => {
        return clearDB();
    });

    it('getTableByAddr', () => {
        expect(Address.getTableByAddr('1PBiTUtwhduqQpM16NHrjTZk2wLN1TUs6W'))
            .be.eq('addresses_0047');
        expect(Address.getTableByAddr('1M2AacMQ6FYRBEXxgyXnH92tgHnu1wh6s5'))
            .be.eq('addresses_0031');
    });

    it('make Address from addr #1', done => {
        Address.make('2MyG74xRkyusNkLmLnuTqmLHgSYhHrwDstP')
            .then(inst => {
                expect(inst).be.not.null;
                expect(inst.attrs.id).be.eq(6);
                done();
            });
    });

    it('make Address from addr #2', () => {
        return expect(Address.make('2MyG74xRkyusNkLmLnuTqmLHgSYhHrwDst1')).eventually.be.null;
    });

    it('load address txs');
});