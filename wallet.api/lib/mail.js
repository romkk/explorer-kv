let request = require('request-promise');
let config = require('config');
let log = require('debug')('wallet:lib:mail');
let fs = require('fs');

module.exports = async (receiver, filePath, fileName) => {
    const ENDPOINT = config.get('mailEndpoint');
    try {
        return (await request({
            uri: 'http://localhost:3002/sendmail',
            method: 'post',
            formData: {
                to: receiver,
                subject: 'hello world',
                content: '<h1>hello world</h1>',
                attachment: {
                    value: fs.createReadStream(filePath),
                    options: {
                        filename: fileName,
                        contentType: 'application/zip'
                    }
                }
            },
            json: true
        }));
    } catch (err) {
        log(`发送邮件失败, to = ${receiver}`);
        throw err;
    }
};