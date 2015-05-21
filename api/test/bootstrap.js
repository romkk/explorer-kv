require('dotenv').load({ path: `${__dirname}/.env` });

var chai = require('chai');
chai.use(require("chai-as-promised"));

global.expect = chai.expect;
