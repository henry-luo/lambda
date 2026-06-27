var zlib = require('node:zlib');
var Buffer = require('buffer');

function checkSync(label, fn) {
  try {
    fn();
    console.log(label + ' threw:', false);
  } catch (err) {
    console.log(label + ' code:', err && err.code);
    console.log(label + ' errno:', err && err.errno);
    console.log(label + ' has message:', typeof err.message === 'string' && err.message.length > 0);
  }
}

var bad = Buffer.from('not compressed data');

checkSync('gunzipSync invalid', function() {
  zlib.gunzipSync(bad);
});

checkSync('inflateSync invalid', function() {
  zlib.inflateSync(bad);
});

zlib.gunzip(bad, function(err, result) {
  console.log('gunzip cb code:', err && err.code);
  console.log('gunzip cb errno:', err && err.errno);
  console.log('gunzip cb result undefined:', result === undefined);
});

zlib.inflate(bad, function(err, result) {
  console.log('inflate cb code:', err && err.code);
  console.log('inflate cb errno:', err && err.errno);
  console.log('inflate cb result undefined:', result === undefined);
});
