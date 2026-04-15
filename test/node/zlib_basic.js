// zlib basic tests — gzip/gunzip, deflate/inflate
var zlib = require('node:zlib');
var Buffer = require('buffer');

// gzipSync + gunzipSync roundtrip
var input = Buffer.from('hello world');
var compressed = zlib.gzipSync(input);
console.log('gzip compressed:', compressed.length > 0);
console.log('gzip smaller or has header:', compressed.length > 0);

var decompressed = zlib.gunzipSync(compressed);
console.log('gunzip result:', decompressed.toString());

// deflateSync + inflateSync roundtrip
var deflated = zlib.deflateSync(input);
console.log('deflate compressed:', deflated.length > 0);

var inflated = zlib.inflateSync(deflated);
console.log('inflate result:', inflated.toString());

// deflateRawSync + inflateRawSync roundtrip
var rawDeflated = zlib.deflateRawSync(input);
console.log('deflateRaw compressed:', rawDeflated.length > 0);

var rawInflated = zlib.inflateRawSync(rawDeflated);
console.log('inflateRaw result:', rawInflated.toString());

// unzipSync (auto-detect gzip)
var unzipped = zlib.unzipSync(compressed);
console.log('unzip result:', unzipped.toString());

// zlib.constants
console.log('Z_NO_FLUSH:', zlib.constants.Z_NO_FLUSH === 0);
console.log('Z_FINISH:', zlib.constants.Z_FINISH === 4);

// larger data roundtrip
var large = Buffer.from('The quick brown fox jumps over the lazy dog. Repeated. The quick brown fox jumps over the lazy dog.');
var lComp = zlib.gzipSync(large);
var lDecomp = zlib.gunzipSync(lComp);
console.log('large roundtrip:', lDecomp.toString() === large.toString());
