const assert = require('assert');
const stream = require('stream');

assert(Object.hasOwn(stream.Readable.prototype, 'readableEnded'));
assert(Object.hasOwn(stream.Writable.prototype, 'writableEnded'));
assert(Object.hasOwn(stream.Writable.prototype, 'writableFinished'));

const readable = new stream.Readable({
  read() {
    assert.strictEqual(readable.readableEnded, false);
    readable.push('x');
    assert.strictEqual(readable.readableEnded, false);
    readable.push(null);
    assert.strictEqual(readable.readableEnded, false);
  }
});

const pausedReadable = new stream.Readable({ read() {} });
assert.strictEqual(pausedReadable.isPaused(), false);
pausedReadable.pause();
assert.strictEqual(pausedReadable.isPaused(), true);
pausedReadable.resume();
assert.strictEqual(pausedReadable.isPaused(), false);

let sawData = false;
let sawEnd = false;
readable.on('end', () => {
  sawEnd = true;
  assert.strictEqual(readable.readableEnded, true);
  assert.strictEqual(readable.readable, false);
});
readable.on('data', () => {
  sawData = true;
  assert.strictEqual(readable.readableEnded, false);
});

const writable = new stream.Writable({
  write(chunk, encoding, cb) {
    assert.strictEqual(writable._writableState.ended, false);
    assert.strictEqual(writable.writableEnded, false);
    cb();
  }
});

let sawPrefinish = false;
let sawFinish = false;
let sawEndCallback = false;
writable.on('prefinish', () => {
  sawPrefinish = true;
  assert.strictEqual(writable._writableState.ended, true);
  assert.strictEqual(writable.writableEnded, true);
});
writable.end('done', () => {
  sawEndCallback = true;
  assert.strictEqual(writable.writable, false);
  assert.strictEqual(writable.writableFinished, true);
});
writable.on('finish', () => {
  sawFinish = true;
  assert.strictEqual(writable._writableState.finished, true);
});

process.nextTick(() => {
  assert(sawData);
  assert(sawEnd);
  assert(sawPrefinish);
  assert(sawEndCallback);
  assert(sawFinish);
  console.log('stream state ok');
});
