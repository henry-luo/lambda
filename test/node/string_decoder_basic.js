// string_decoder basic tests
var sd = require('node:string_decoder');
var Buffer = require('buffer');

// Create a decoder
var decoder = sd.StringDecoder('utf8');
console.log('decoder created:', decoder !== undefined);

// write complete ASCII string
var result = decoder.write(Buffer.from('hello'));
console.log('write hello:', result);

// write more data
var result2 = decoder.write(Buffer.from(' world'));
console.log('write world:', result2);

// end with no data
var endResult = decoder.end();
console.log('end result:', endResult);

// new decoder - write then end with buffer
var d2 = sd.StringDecoder('utf8');
var r2 = d2.write(Buffer.from('ABC'));
console.log('write ABC:', r2);

var r3 = d2.end(Buffer.from('DEF'));
console.log('end DEF:', r3);
