var zlib = require('node:zlib');
var Buffer = require('buffer');

function collect(stream, label, done) {
  var chunks = [];
  stream.on('data', function(chunk) {
    chunks.push(chunk);
  });
  stream.on('error', function(err) {
    console.log(label + ' error:', err && err.message);
  });
  stream.on('end', function() {
    done(Buffer.concat(chunks));
  });
}

console.log('createGzip exists:', typeof zlib.createGzip === 'function');
console.log('createGunzip exists:', typeof zlib.createGunzip === 'function');
console.log('createDeflate exists:', typeof zlib.createDeflate === 'function');
console.log('createInflate exists:', typeof zlib.createInflate === 'function');

var gzip = zlib.createGzip();
console.log('gzip write exists:', typeof gzip.write === 'function');
console.log('gzip end exists:', typeof gzip.end === 'function');
console.log('gzip pipe exists:', typeof gzip.pipe === 'function');
console.log('gzip instanceof Gzip:', gzip instanceof zlib.Gzip);

collect(gzip, 'gzip', function(compressed) {
  console.log('gzip stream compressed:', compressed.length > 0);
  var gunzip = zlib.createGunzip();
  console.log('gunzip instanceof Gunzip:', gunzip instanceof zlib.Gunzip);
  collect(gunzip, 'gunzip', function(decompressed) {
    console.log('gunzip stream:', decompressed.toString());

    var deflate = zlib.createDeflate();
    console.log('deflate instanceof Deflate:', deflate instanceof zlib.Deflate);
    collect(deflate, 'deflate', function(deflated) {
      var inflate = zlib.createInflate();
      console.log('inflate instanceof Inflate:', inflate instanceof zlib.Inflate);
      collect(inflate, 'inflate', function(inflated) {
        console.log('inflate stream:', inflated.toString());
      });
      inflate.end(deflated);
    });
    deflate.end('deflate stream hello');
  });
  gunzip.end(compressed);
});

gzip.end(Buffer.from('gzip stream hello'));
