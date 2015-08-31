module.exports = {
  database: {
    host: process.env.DATABASE_HOST,
    user: process.env.DATABASE_USER,
    pass: process.env.DATABASE_PASS,
    port: process.env.DATABASE_PORT,
    name: process.env.DATABASE_NAME
  },

  ssdb: {
    host: process.env.SSDB_HOST,
    port: process.env.SSDB_PORT
  },

  bitcoind: {
    host: process.env.BITCOIND_HOST,
    port: process.env.BITCOIND_PORT,
    user: process.env.BITCOIND_USER,
    pass: process.env.BITCOIND_PASS
  }
};