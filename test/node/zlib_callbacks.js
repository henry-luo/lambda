var zlib = require('node:zlib');
var Buffer = require('buffer');

var input = Buffer.from('callback hello');

zlib.gzip(input, function(err, compressed) {
  console.log('gzip cb err null:', err === null);
  zlib.gunzip(compressed, function(err, decompressed) {
    console.log('gunzip cb:', err === null, decompressed.toString());
  });
});

zlib.deflate('deflate hello', { level: 9 }, function(err, compressed) {
  console.log('deflate cb err null:', err === null);
  zlib.inflate(compressed, {}, function(err, decompressed) {
    console.log('inflate cb:', err === null, decompressed.toString());
  });
});

zlib.deflateRaw(input, function(err, compressed) {
  zlib.inflateRaw(compressed, function(err, decompressed) {
    console.log('raw cb:', err === null, decompressed.toString());
  });
});

var wrappedDeflate = zlib.deflateSync('wrapped deflate');
var gzip = zlib.gzipSync('wrapped gzip');

console.log('unzipSync deflate:', zlib.unzipSync(wrappedDeflate).toString());
console.log('unzipSync gzip:', zlib.unzipSync(gzip).toString());

zlib.unzip(wrappedDeflate, function(err, decompressed) {
  console.log('unzip cb deflate:', err === null, decompressed.toString());
});

zlib.unzip(gzip, {}, function(err, decompressed) {
  console.log('unzip cb gzip:', err === null, decompressed.toString());
});
