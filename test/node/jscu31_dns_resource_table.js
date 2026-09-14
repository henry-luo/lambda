// JSCU31: DNS requests and their scheduled completion use the context resource table.
const dns = require('node:dns');

dns.resolve4('localhost', (error, addresses) => {
  console.log(error === null, Array.isArray(addresses), addresses.length > 0);
});
