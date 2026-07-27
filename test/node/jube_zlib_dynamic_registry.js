const zlib = require('zlib');
const source = Buffer.from('jube-zlib');
const compressed = zlib.gzipSync(source);

console.log(zlib === require('node:zlib'));
console.log(zlib.gunzipSync(compressed).toString());
console.log(zlib.inflateSync(zlib.deflateSync(source)).toString());
console.log(zlib.inflateRawSync(zlib.deflateRawSync(source)).toString());
console.log(zlib.unzipSync(compressed).toString());
console.log(zlib.crc32(Buffer.from('jube')));
console.log(zlib.crc32 === require('zlib').crc32);

zlib.gzip(source, function(error, encoded) {
  console.log(error === null, encoded.length > 0);
  zlib.gunzip(encoded, function(decodedError, decoded) {
    console.log(decodedError === null, decoded.toString());
  });
});
zlib.gunzip(Buffer.from('bad'), function(error, decoded) {
  console.log(error.code, error.errno, decoded === undefined);
});
console.log('callbacks queued');

const view = new DataView(new ArrayBuffer(4));
console.log(view.byteLength);
console.log(zlib.crc32(view));
