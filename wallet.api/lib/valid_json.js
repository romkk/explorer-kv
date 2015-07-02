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
    },

    device: {
        type: 'object',
        required: ['device_id', 'os'],
        properties: {
            device_id: {
                type: 'string',
                minLength: 1
            },
            os: {
                type: 'string',
                'enum': ['iOS', 'Android', 'Windows', 'Other']
            }
        }
    },

    createMultiSignatureAccount: {
        type: 'object',
        required: ['creator_name', 'creator_pubkey', 'account_name', 'm', 'n'],
        properties: {
            creator_name: {
                type: 'string',
                minLength: 1,
                maxLength: 256
            },
            creator_pubkey: {
                type: 'string',
                minLength: 1,
                maxLength: 150
            },
            account_name: {
                type: 'string',
                minLength: 1,
                maxLength: 150
            },
            m: {
                type: 'integer',
                maximum: 256,
                minimum: 2
            },
            n: {
                type: 'integer',
                maximum: 255,
                minimum: 1
            }
        }
    },

    updateMultiSignatureAccount: {
        type: 'object',
        required: ['name', 'pubkey'],
        properties: {
            name: {
                type: 'string',
                minLength: 1,
                maxLength: 256
            },
            pubkey: {
                type: 'string',
                minLength: 1,
                maxLength: 150
            }
        }
    },

    createMultiSignatureTx: {
        type: 'object',
        required: ['rawtx', 'note'],
        properties: {
            rawtx: {
                type: 'string',
                minLength: 1,
                maxLength: 65536
            },
            note: {
                type: 'string',
                minLength: 0,
                maxLength: 65536
            }
        }
    },

    updateMultiSignatureTx: {
        type: 'object',
        required: [],
        properties: {
            original: {
                type: 'string',
                minLength: 1
            },
            signed: {
                type: 'string',
                minLength: 1
            },
            status: {
                type: 'string',
                'enum': ['APPROVED', 'DENIED']
            }
        }
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