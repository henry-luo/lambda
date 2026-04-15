// stream basic tests — constructors and sync operations
var stream = require('node:stream');

// Readable constructor exists
var r = new stream.Readable();
console.log('Readable exists:', typeof r === 'object');

// Writable constructor exists
var w = new stream.Writable();
console.log('Writable exists:', typeof w === 'object');

// Duplex constructor exists
var d = new stream.Duplex();
console.log('Duplex exists:', typeof d === 'object');

// Transform constructor exists
var t = new stream.Transform();
console.log('Transform exists:', typeof t === 'object');

// PassThrough constructor exists
var pt = new stream.PassThrough();
console.log('PassThrough exists:', typeof pt === 'object');

// pipeline function exists
console.log('pipeline exists:', typeof stream.pipeline === 'function');

// finished function exists
console.log('finished exists:', typeof stream.finished === 'function');

// Readable.from exists
console.log('Readable.from exists:', typeof stream.Readable.from === 'function');

// Readable has on method
console.log('Readable has on:', typeof r.on === 'function');

// Writable has write method
console.log('Writable has write:', typeof w.write === 'function');

// Writable has end method
console.log('Writable has end:', typeof w.end === 'function');

// Readable has pipe method
console.log('Readable has pipe:', typeof r.pipe === 'function');

// Readable has read method
console.log('Readable has read:', typeof r.read === 'function');

// Readable has push method
console.log('Readable has push:', typeof r.push === 'function');

// Writable has destroy method
console.log('Writable has destroy:', typeof w.destroy === 'function');
