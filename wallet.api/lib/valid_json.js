var validate = require('jsonschema').validate;
var restify = require('restify');
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

        var result = validate(json, schema[schemaName]);

        if (result.errors.length) {
            return next(new restify.BadRequestError(`${result.errors[0].property} ${result.errors[0].message}`));
        }

        next();
    };
};