let restify = require('restify');
let qr = require('qr-image');
let _ = require('lodash');
let request = require('request-promise');
let config = require('config');
let url = require('url');

module.exports = server => {
    server.get('/qr-code', async (req, res, next) => {
        req.checkQuery('msg', 'should be a valid string').isLength(1);
        req.sanitize('msg').toString();

        req.checkQuery('size', 'should be a valid int').optional().isInt({ min: 1, max: 20 });
        req.sanitize('size').toInt();

        req.checkQuery('ec_level', 'should be a valid string').optional().isIn(['L', 'M', 'Q', 'H']);
        req.sanitize('ec_level').toString();

        var errors = req.validationErrors();
        if (errors) {
            return next(new restify.InvalidArgumentError({
                message: errors
            }));
        }

        let msg = req.params.msg;
        let size = _.get(req, 'params.size', 5);
        let ecLevel = _.get(req, 'params.ec_level', 'M');

        res.header('Content-Type', 'image/png');
        qr.image(msg, {
            ec_level: ecLevel,
            size: size,
            type: 'png',
            margin: 1
        }).pipe(res);

        return next();
    });

    server.get('/qr-code-page', async (req, res, next) => {
        req.checkQuery('msg', 'should be a valid string').isLength(1);
        req.sanitize('msg').toString();
        var errors = req.validationErrors();
        if (errors) {
            return next(new restify.InvalidArgumentError({
                message: errors
            }));
        }

        let msg = req.params.msg;
        let imageSrc = config.get('qrCodeEndpoint') + '?msg=' + msg + '&size=10';
        let html = `<!DOCTYPE html>
        <html>
            <head>
                <meta charset="utf-8">
                <meta http-equiv="X-UA-Compatible" content="IE=edge">
                <meta name="viewport" content="width=device-width, initial-scale=1">
                <title>Bitmain QR Code Service</title>
                <style>
                    html, body { height: 100%; }
                    body { margin: 0; }
                    .container { display: table; height: 100%; max-height: 600px; margin-left: auto; margin-right: auto; }
                    .container-inner { display: table-cell; vertical-align: middle; text-align: center; }
                    .desc { font-size: 12px; font-family: 'Helvetica Neue', Helvetica, Arial, sans-serif; color: #666; }
                </style>
            </head>
            <body>
                <div class="container">
                    <div class="container-inner">
                        <div class="img">
                            <img src="${imageSrc}" alt="${msg}"/>
                        </div>
                        <p class="desc">请使用微信等工具扫描</p>
                    </div>
                </div>
            </body>
        </html>
        `;

        res.writeHead(200, {
            'Content-Type': 'text/html;charset=utf-8'
        });
        res.write(html);
        res.end();
        next();
    });
};