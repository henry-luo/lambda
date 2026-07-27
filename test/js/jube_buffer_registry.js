const buffer = require('buffer');
const nodeBuffer = require('node:buffer');

console.log(buffer === nodeBuffer, buffer.default === buffer);
console.log(Buffer === buffer.Buffer, typeof buffer.Buffer.from, typeof buffer.Buffer.alloc);
console.log(buffer.Buffer.from('jube').toString());
