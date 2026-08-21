const dns = require('dns');
const crypto = require('crypto');

console.log(typeof dns.lookup, typeof dns.promises.lookup, dns === require('dns'));
console.log(crypto.createHash('sha256').update('jube').digest('hex'));
console.log(globalThis.crypto === crypto, typeof process.permission);
