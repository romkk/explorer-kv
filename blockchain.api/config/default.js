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
  }
};