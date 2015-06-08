var Address = require('../../lib/address');

describe('address', ()=> {
    it('getTableByAddr', () => {
        expect(Address.getTableByAddr('1PBiTUtwhduqQpM16NHrjTZk2wLN1TUs6W'))
            .be.eq('addresses_0047');
        expect(Address.getTableByAddr('1M2AacMQ6FYRBEXxgyXnH92tgHnu1wh6s5'))
            .be.eq('addresses_0031');
    });
});