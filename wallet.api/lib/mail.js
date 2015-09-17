let request = require('request-promise');
let config = require('config');
let log = require('debug')('wallet:lib:mail');
let moment = require('moment');
let fs = require('fs');

module.exports = async (lang, receiver, filePath, fileName) => {
    const ENDPOINT = config.get('mailEndpoint');

    let subject = {
        'zh-cn': `BTC 钱包备份文本`,
        'en': `BTC Wallet Backup File`
    };

    let content = {
        'zh-cn': `<p>附件为您的钱包加密备份文件。您可使用附件中的文件通过 BTC 客户端恢复钱包的所有设置和余额。</p>`,
        'en': `Attached is your BTC wallet back file, you can use this file to recover all the bitcoins and wallet setups of your BTC wallet.`
    };
    try {
        let ret = await request({
            uri: ENDPOINT,
            method: 'post',
            formData: {
                target: receiver,
                subject: subject[lang],
                mail_content: content[lang],
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

        return ret;
    } catch (err) {
        log(`发送邮件失败, to = ${receiver}`);
        console.error(err.stack);
        throw err;
    }
};