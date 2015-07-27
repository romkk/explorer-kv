let request = require('request-promise');
let moment = require('moment');
let _ = require('lodash');
let crypto = require('crypto');
let querystring = require('querystring');
let uuid = require('node-uuid');
let log = require('debug')('wallet:lib:oss');
let config = require('config');

function encode(str) {
    return encodeURIComponent(str)
        .replace(/!/g, '%21')
        .replace(/\*/g, '%2A')
        .replace(/'/g, '%27')
        .replace(/\(/g, '%28')
        .replace(/\)/g, '%29');
}

class STSToken {
    constructor(wid_internal) {
        this.wid_internal = wid_internal;
        this.body = this.signature = null;
    }

    policy() {
        return {
            Version: '1',
            Statement: [
                {
                    Action: [
                        'oss:PutObject',
                        'oss:GetObject',
                        'oss:HeadObject'
                    ],
                    Resource: `acs:oss:*:${config.get('oss.bucketOwnerId')}:${config.get('oss.bucket')}/${this.wid_internal}/*`,
                    Effect: "Allow"
                }
            ]
        };
    }

    prepareBody() {
        this.body = {
            Format: 'JSON',
            Version: '2015-04-01',
            AccessKeyId: config.get('oss.rootAccessKeyId'),
            SignatureMethod: 'HMAC-SHA1',
            SignatureVersion: '1.0',
            SignatureNonce: uuid.v4(),
            //SignatureNonce: '637465bb-42bf-491c-b9d3-5965c452bb1d',
            Timestamp: moment.utc().format('YYYY-MM-DDThh:mm:ss[Z]'),
            //Timestamp: '2015-07-27T09:14:26Z',
            Action: 'GetFederationToken',
            StsVersion: '1',
            Name: String(this.wid_internal),
            Policy: JSON.stringify(this.policy()),
            DurationSeconds: String(config.get('oss.durationSeconds'))
        };
        return this;
    }

    sign() {
        let qkeys = Object.keys(this.body).sort();
        let canonicalizedQueryString = qkeys.map(k => {
            let v = this.body[k];
            return encode(k) + '=' + encode(v);
        }).join('&');
        //console.log(canonicalizedQueryString);
        let stringToSign = 'POST' + '&' +
            encode('/') + '&' +
            encode(canonicalizedQueryString);
        //console.log(stringToSign);
        this.signature = crypto.createHmac('sha1', config.get('oss.rootAccessKeySecret') + '&').update(stringToSign).digest('base64');
        return this;
    }

    async req() {
        log(`开始获取 STS Token, wid = ${this.wid_internal}`);
        let res;
        let body = _.extend({}, this.body, {
            Signature: this.signature
        });
        try {
            res = await request({
                url: 'https://sts.aliyuncs.com',
                method: 'POST',
                form: body
            });
        } catch (err) {
            log(`获取 STS Token 出错`);
            throw err;
        }

        return JSON.parse(res);
    }

    static async make(wid) {
        let instance = new STSToken(wid);
        return (await instance.prepareBody().sign().req());
    }
}

module.exports = STSToken;