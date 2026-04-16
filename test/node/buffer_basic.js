// buffer module basic tests
import { Buffer } from 'buffer';

// Buffer.alloc
var buf1 = Buffer.alloc(10);
console.log(Buffer.isBuffer(buf1));
console.log(buf1.length);

// Buffer.from string (utf-8)
var buf2 = Buffer.from('hello');
console.log(buf2.length);
console.log(Buffer.toString(buf2));

// Buffer.from hex
var buf3 = Buffer.from('48656c6c6f', 'hex');
console.log(Buffer.toString(buf3));

// Buffer.from base64
var buf4 = Buffer.from('SGVsbG8=', 'base64');
console.log(Buffer.toString(buf4));

// toString with hex encoding
var buf5 = Buffer.from('Hi');
console.log(Buffer.toString(buf5, 'hex'));

// toString with base64 encoding
console.log(Buffer.toString(buf5, 'base64'));

// Buffer.from array
var buf6 = Buffer.from([72, 101, 108, 108, 111]);
console.log(Buffer.toString(buf6));

// Buffer.concat
var buf7 = Buffer.concat([Buffer.from('He'), Buffer.from('llo')]);
console.log(Buffer.toString(buf7));

// Buffer.byteLength
console.log(Buffer.byteLength('hello'));

// Buffer.equals
console.log(Buffer.equals(Buffer.from('abc'), Buffer.from('abc')));
console.log(Buffer.equals(Buffer.from('abc'), Buffer.from('xyz')));

// Buffer.compare
console.log(Buffer.compare(Buffer.from('abc'), Buffer.from('abc')));
console.log(Buffer.compare(Buffer.from('abc'), Buffer.from('abd')));

// Buffer.indexOf
var buf8 = Buffer.from('hello world');
console.log(Buffer.indexOf(buf8, 'world'));
console.log(Buffer.indexOf(buf8, 111));

// Buffer.fill
var buf9 = Buffer.alloc(5);
Buffer.fill(buf9, 65);
console.log(Buffer.toString(buf9));
