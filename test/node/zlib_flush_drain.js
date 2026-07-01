const assert = require('assert');
const zlib = require('zlib');

const bigData = Buffer.alloc(10240, 'x');
const deflater = zlib.createDeflate({ level: 0, highWaterMark: 16 });

let flushCount = 0;
let drainCount = 0;
const flush = deflater.flush;
deflater.flush = function(kind, callback) {
  flushCount++;
  return flush.call(this, kind, callback);
};

deflater.write(bigData);
const beforeFlush = deflater._writableState.needDrain;
let afterFlush = deflater._writableState.needDrain;

deflater.on('data', () => {});
deflater.flush((err) => {
  assert.ifError(err);
  afterFlush = deflater._writableState.needDrain;
});
deflater.on('drain', () => {
  drainCount++;
});

process.once('exit', () => {
  assert.strictEqual(beforeFlush, true);
  assert.strictEqual(afterFlush, false);
  assert.strictEqual(drainCount, 1);
  assert.strictEqual(flushCount, 1);
  console.log('zlib flush drain ok');
});
