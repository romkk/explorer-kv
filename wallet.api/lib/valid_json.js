var JaySchema = require('jayschema');
var restify = require('restify');
var bitcore = require('bitcore');

var validator = new JaySchema();

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
    },

    tx: {
        type: 'object',
        properties: {
            'from': {
                type: 'array',
                maxItems: 1024,
                minItems: 1,
                uniqueItems: true,
                items: {
                    type: 'string',
                    format: 'btc-address'
                }
            },
            to: {
                type: 'array',
                maxItems: 1024,
                minItems: 1,
                uniqueItems: true,
                items: {
                    type: 'object',
                    properties: {
                        addr: {
                            type: 'string',
                            format: 'btc-address'
                        },
                        amount: {
                            type: 'integer',
                            minimum: 0
                        }
                    },
                    required: ['addr', 'amount']
                }
            },
            fee_per_kb: {
                type: 'integer',
                'enum': [10000]
            }
        },
        required: ['fee_per_kb', 'from', 'to']
    },

    verifyAuth: {
        type: 'object',
        required: ['challenge', 'signature', 'address'],
        properties: {
            challenge: {
                type: 'string'
            },
            signature: {
                type: 'string'
            },
            address: {
                type: 'string',
                format: 'btc-address'
            }
        }
    }

};

function formatError(e) {
    if (e.kind == 'ObjectValidationError') {
        return `${e.kind} - ${e.desc}`;
    }

    return `position: ${e.instanceContext}, rule: ${e.constraintName}`;
}

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
            return next(new restify.BadRequestError(formatError(e)));
        }

        next();
    };
};