const stream = require('stream');
const promises = require('stream/promises');
const web = require('stream/web');
const consumers = require('stream/consumers');
const iter = require('stream/iter');

console.log(stream === require('node:stream'));
console.log(promises === stream.promises, consumers === stream);
console.log(typeof web.ReadableStream, typeof consumers.buffer, typeof iter.from);
