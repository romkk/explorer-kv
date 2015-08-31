var JaySchema = require('jayschema');
var restify = require('restify');
var bitcore = require('bitcore');
var RestError = require('restify').RestError;
var validator = new JaySchema();
var util = require('util');

function ValidationError(cause) {
    restify.BadRequestError.call(this, 'validation error');
    this.body.description = cause;
}

util.inherits(ValidationError, restify.BadRequestError);

validator.addFormat('btc-address', v => {
    return bitcore.Address.isValid(v) ? null : 'must be valid btc address';
});

var schema = {

    txPublish: {
        type: 'object',
        properties: {
            hex: {
                type: 'string',
                minLength: 1
            }
        },
        required: ['hex']
    }

};

module.exports = (schemaName) => {
    if (!schema[schemaName]) {
        throw new Error(`invalid schemaName = ${schemaName}`);
    }

    return (req, res, next) => {
        var json = req.body;

        if (!json) {
            return next(new restify.BadRequestError('Reqeust body can not be empty'));
        }

        var errors = validator.validate(json, schema[schemaName]);

        if (errors.length) {
            let e = errors[0];
            return next(new ValidationError(e));
        }

        next();
    };
};