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

function splitCompressed(buffer) {
  return [
    buffer.slice(0, 1),
    buffer.slice(1, 5),
    buffer.slice(5)
  ];
}

function writePieces(stream, pieces) {
  for (var i = 0; i + 1 < pieces.length; i++) {
    stream.write(pieces[i]);
  }
  stream.end(pieces[pieces.length - 1]);
}

function streamRoundTrip(label, compressor, decompressor, pieces, next) {
  collect(compressor, label + ' compressor', function(compressed) {
    console.log(label + ' split compressed:', compressed.length > 0);
    collect(decompressor, label + ' decompressor', function(decompressed) {
      console.log(label + ' split result:', decompressed.toString());
      if (next) next();
    });
    writePieces(decompressor, splitCompressed(compressed));
  });
  writePieces(compressor, pieces);
}

function runSplitStreaming() {
  streamRoundTrip('gzip', zlib.createGzip(), zlib.createGunzip(),
    [Buffer.from('split '), Buffer.from('gzip '), Buffer.from('hello')],
    function() {
      streamRoundTrip('deflate', zlib.createDeflate(), zlib.createInflate(),
        [Buffer.from('split '), Buffer.from('deflate '), Buffer.from('hello')],
        function() {
          streamRoundTrip('raw', zlib.createDeflateRaw(), zlib.createInflateRaw(),
            [Buffer.from('split '), Buffer.from('raw '), Buffer.from('hello')],
            function() {
              streamRoundTrip('unzip', zlib.createDeflate(), zlib.createUnzip(),
                [Buffer.from('split '), Buffer.from('unzip '), Buffer.from('hello')],
                runConcatenatedUnzip);
            });
        });
    });
}

function runConcatenatedUnzip() {
  var data = Buffer.concat([
    zlib.gzipSync('abc'),
    zlib.gzipSync('def')
  ]);
  var unzip = zlib.createUnzip();
  collect(unzip, 'concat unzip', function(result) {
    console.log('unzip concatenated one-byte:', result.toString());
  });
  for (var i = 0; i < data.length; i++) {
    unzip.write(Buffer.from([data[i]]));
  }
  unzip.end();
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
        runSplitStreaming();
      });
      inflate.end(deflated);
    });
    deflate.end('deflate stream hello');
  });
  gunzip.end(compressed);
});

gzip.end(Buffer.from('gzip stream hello'));
