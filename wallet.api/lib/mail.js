let request = require('request-promise');
let config = require('config');
let log = require('debug')('wallet:lib:mail');
let moment = require('moment');
let fs = require('fs');

module.exports = async (receiver, filePath, fileName) => {
    const ENDPOINT = config.get('mailEndpoint');
    try {
        let ret = await request({
            uri: ENDPOINT,
            method: 'post',
            formData: {
                target: receiver,
                subject: '您的钱包备份文件',
                mail_content: `<p>您好</p><p>您于 ${moment.utc().format('YYYY-MM-DD HH:mm:ss')} 备份了您的 Bitmain Wallet，备份文件见附件。</p>`,
                app_name: config.get('mailAppname'),
                attach_file: {
                    value: fs.createReadStream(filePath),
                    options: {
                        filename: fileName,
                        contentType: 'application/zip'
                    }
                }
            },
            json: true
        });

        if (ret.result_code != 0) {
            let e = new Error();
            e.message = ret.msg;
            e.code = ret.result_code;
            throw e;
        }
    } catch (err) {
        log(`发送邮件失败, to = ${receiver}`);
        console.error(err.stack);
        throw err;
    }
};