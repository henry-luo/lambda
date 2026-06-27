var zlib = require('node:zlib');

var afterReturn = false;
var events = [];

zlib.gzip('async order', function(err, compressed) {
  events.push('gzip');
  console.log('gzip after return:', afterReturn);
  console.log('gzip err null:', err === null);
  console.log('gzip text:', zlib.gunzipSync(compressed).toString());

  zlib.deflate('nested deflate', function(err, compressed) {
    events.push('deflate');
    console.log('deflate err null:', err === null);
    console.log('deflate text:', zlib.inflateSync(compressed).toString());
    console.log('events final:', events.join(','));
  });
});

events.push('after-call');
afterReturn = true;
console.log('after call events:', events.join(','));

process.nextTick(function() {
  events.push('manual');
  console.log('manual nextTick events:', events.join(','));
});

