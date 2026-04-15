// dns basic tests — lookupSync
var dns = require('node:dns');

// lookupSync for localhost
var result = dns.lookupSync('localhost');
console.log('lookupSync type:', typeof result);
console.log('lookupSync is string:', typeof result === 'string');
