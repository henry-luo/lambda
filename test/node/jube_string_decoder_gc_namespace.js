const bare = require('string_decoder');
const prefixed = require('node:string_decoder');
const decoder = bare.StringDecoder('utf8');

console.log(bare === prefixed);
console.log(decoder.end() === '');
