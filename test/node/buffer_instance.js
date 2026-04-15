// Buffer instance method tests — verify buf.method() works (not just Buffer.method(buf))
import Buffer from 'buffer';

// toString instance method
var buf = Buffer.from('Hello');
console.log(buf.toString());

// readUInt8 instance
console.log(buf.readUInt8(0));

// slice + toString chain
console.log(buf.slice(0, 3).toString());

// write methods
var buf2 = Buffer.alloc(4);
buf2.writeUInt8(65, 0);
buf2.writeUInt8(66, 1);
console.log(buf2.readUInt8(0));
console.log(buf2.readUInt8(1));

// writeUInt32BE / readUInt32BE
var buf3 = Buffer.alloc(4);
buf3.writeUInt32BE(305419896, 0);
console.log(buf3.readUInt32BE(0));

// writeDoubleBE / readDoubleBE
var buf4 = Buffer.alloc(8);
buf4.writeDoubleBE(3.14, 0);
console.log(buf4.readDoubleBE(0));

// equals / compare
var a = Buffer.from('abc');
var b = Buffer.from('abc');
var c = Buffer.from('def');
console.log(a.equals(b));
console.log(a.equals(c));
console.log(a.compare(b));

// indexOf / includes (Buffer byte-string search)
var buf5 = Buffer.from('hello world');
console.log(buf5.indexOf('world'));
console.log(buf5.includes('hello'));

// fill + toString
var buf6 = Buffer.alloc(3);
buf6.fill(88);
console.log(buf6.toString());

// readUIntBE variable-width
var buf7 = Buffer.alloc(3);
buf7.writeUInt8(1, 0);
buf7.writeUInt8(2, 1);
buf7.writeUInt8(3, 2);
console.log(buf7.readUIntBE(0, 3));

// readInt16BE / writeInt16BE
var buf8 = Buffer.alloc(2);
buf8.writeInt16BE(-1234, 0);
console.log(buf8.readInt16BE(0));

// subarray
var buf9 = Buffer.from('abcdef');
console.log(buf9.subarray(2, 4).toString());

// copy
var src = Buffer.from('Hello');
var dst = Buffer.alloc(5);
src.copy(dst);
console.log(dst.toString());

// static Buffer.allocUnsafeSlow
var buf10 = Buffer.allocUnsafeSlow(4);
console.log(buf10.length);
