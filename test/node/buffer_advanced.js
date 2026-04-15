// buffer advanced tests — toJSON, swap, lastIndexOf, BigInt64, writeFloat
var Buffer = require('buffer');

// toJSON
var buf = Buffer.from('Hi');
console.log('toJSON exists:', typeof buf.toJSON === 'function');

// swap16
var s16 = Buffer.from([0x01, 0x02, 0x03, 0x04]);
s16.swap16();
console.log('swap16 [0]:', s16[0]);
console.log('swap16 [1]:', s16[1]);
console.log('swap16 [2]:', s16[2]);
console.log('swap16 [3]:', s16[3]);

// swap32
var s32 = Buffer.from([0x01, 0x02, 0x03, 0x04]);
s32.swap32();
console.log('swap32 [0]:', s32[0]);
console.log('swap32 [3]:', s32[3]);

// lastIndexOf
var lbuf = Buffer.from('abcabc');
console.log('lastIndexOf c:', lbuf.lastIndexOf('c'));

// writeFloatBE / readFloatBE
var fb = Buffer.alloc(4);
fb.writeFloatBE(1.5, 0);
console.log('readFloatBE:', fb.readFloatBE(0));

// writeFloatLE / readFloatLE
var fl = Buffer.alloc(4);
fl.writeFloatLE(2.5, 0);
console.log('readFloatLE:', fl.readFloatLE(0));

// writeDoubleBE / readDoubleBE
var db = Buffer.alloc(8);
db.writeDoubleBE(123.456, 0);
var dv = db.readDoubleBE(0);
console.log('readDoubleBE:', dv);

// writeDoubleLE / readDoubleLE
var dl = Buffer.alloc(8);
dl.writeDoubleLE(789.012, 0);
console.log('readDoubleLE:', dl.readDoubleLE(0));

// readUInt16BE / writeUInt16BE
var u16 = Buffer.alloc(2);
u16.writeUInt16BE(0x1234, 0);
console.log('readUInt16BE:', u16.readUInt16BE(0));

// readInt32LE / writeInt32LE
var i32 = Buffer.alloc(4);
i32.writeInt32LE(-12345, 0);
console.log('readInt32LE:', i32.readInt32LE(0));

// variable-width readUIntLE
var vl = Buffer.from([0x01, 0x02, 0x00]);
console.log('readUIntLE 2:', vl.readUIntLE(0, 2));

// variable-width writeIntBE with negative
var vi = Buffer.alloc(3);
vi.writeIntBE(-1, 0, 3);
console.log('readIntBE neg:', vi.readIntBE(0, 3));

// Buffer.isBuffer
console.log('isBuffer true:', Buffer.isBuffer(Buffer.alloc(1)));
console.log('isBuffer false:', Buffer.isBuffer('hello'));

// Buffer.isEncoding
console.log('isEncoding utf8:', Buffer.isEncoding('utf8'));
console.log('isEncoding hex:', Buffer.isEncoding('hex'));

// fill with byte value
var fs = Buffer.alloc(3);
fs.fill(65);
console.log('fill byte:', fs.toString());
