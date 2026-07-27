const bare = require('string_decoder');
const prefixed = require('node:string_decoder');
const Buffer = require('buffer');

const decoder = bare.StringDecoder('utf8');
console.log(bare === prefixed);
console.log(decoder.write(Buffer.from('jube')));
console.log(decoder.end(Buffer.from(' node')));
