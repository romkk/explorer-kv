var helper = require('../../lib/helper');

describe('helper', () => {
    it('convert address to base58', () => {
        expect(helper.addressToHash160('1PBiTUtwhduqQpM16NHrjTZk2wLN1TUs6W')).be.eq('f3598876b67b7decf899d1894a6f2d8ad03b6b6f');
        expect(helper.addressToHash160('1M2AacMQ6FYRBEXxgyXnH92tgHnu1wh6s5')).be.eq('db9aed9d3156203ff129a9a67e19a487789c491f');
    });

    it('tell arg is hash or number', () => {
        expect(helper.paramType('abcedf')).be.eq(helper.constant.HASH_IDENTIFIER);
        expect(helper.paramType('1234abcedf')).be.eq(helper.constant.HASH_IDENTIFIER);
        expect(helper.paramType('1234')).be.eq(helper.constant.ID_IDENTIFIER);
    });
});