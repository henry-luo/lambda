let zlib = require('zlib');
function printBytes(output) {
  console.log(output.length, output[0], output[1], output[2], output[3]);
}
printBytes(zlib.gunzipSync(zlib.gzipSync('jube')));
printBytes(zlib.inflateSync(zlib.deflateSync('jube')));
printBytes(zlib.inflateRawSync(zlib.deflateRawSync('jube')));
printBytes(zlib.unzipSync(zlib.gzipSync('jube')));
