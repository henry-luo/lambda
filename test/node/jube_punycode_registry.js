const nodePunycode = require('node:punycode');
const barePunycode = require('punycode.js');

console.log(nodePunycode === barePunycode);
console.log(typeof nodePunycode.encode);
console.log(typeof nodePunycode.decode);
console.log(nodePunycode.version);
console.log(nodePunycode.default === nodePunycode);
console.log(typeof nodePunycode.ucs2);
console.log(nodePunycode.encode('lambda'));
