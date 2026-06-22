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

const readableListening = new stream.Readable({ read() {} });
assert.strictEqual(readableListening._readableState.readableListening, false);
let sawReadableListening = false;
readableListening.on('readable', () => {
  sawReadableListening = true;
  assert.strictEqual(readableListening._readableState.readableListening, true);
});
readableListening.push('listener state');

const dataReadable = new stream.Readable({ read() {} });
assert.strictEqual(dataReadable._readableState.readableListening, false);
let sawDataReadable = false;
dataReadable.on('data', () => {
  sawDataReadable = true;
  assert.strictEqual(dataReadable._readableState.readableListening, false);
});
dataReadable.push('data state');

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
    assert.strictEqual(writable._writableState.ending, false);
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
  assert.strictEqual(writable._writableState.ending, true);
  assert.strictEqual(writable._writableState.ended, true);
  assert.strictEqual(writable.writableEnded, true);
});
writable.end('done', () => {
  sawEndCallback = true;
  assert.strictEqual(writable._writableState.ending, true);
  assert.strictEqual(writable.writable, false);
  assert.strictEqual(writable.writableFinished, true);
});
writable.on('finish', () => {
  sawFinish = true;
  assert.strictEqual(writable._writableState.finished, true);
});

let corkedWriteCount = 0;
let corkedWritevCount = 0;
const corked = new stream.Writable({
  write(chunk, encoding, cb) {
    corkedWriteCount++;
    cb();
  },
  writev(chunks, cb) {
    corkedWritevCount++;
    assert.strictEqual(chunks.length, 2);
    cb();
  }
});
assert.strictEqual(corked.writableCorked, 0);
assert.strictEqual(corked._writableState.corked, 0);
corked.uncork();
assert.strictEqual(corked.writableCorked, 0);
corked.cork();
assert.strictEqual(corked.writableCorked, 1);
assert.strictEqual(corked._writableState.corked, 1);
corked.cork();
assert.strictEqual(corked.writableCorked, 2);
corked.write('buffered');
assert.strictEqual(corked._writableState.bufferedRequestCount, 1);
corked.uncork();
assert.strictEqual(corked.writableCorked, 1);
assert.strictEqual(corked._writableState.bufferedRequestCount, 1);
corked.write('buffered again');
assert.strictEqual(corked._writableState.bufferedRequestCount, 2);
corked.uncork();
assert.strictEqual(corked.writableCorked, 0);
assert.strictEqual(corked._writableState.bufferedRequestCount, 0);
assert.strictEqual(corkedWritevCount, 1);
assert.strictEqual(corkedWriteCount, 0);
corked.cork();
corked.write('single');
corked.end();
assert.strictEqual(corked.writableCorked, 0);
assert.strictEqual(corked._writableState.bufferedRequestCount, 0);
assert.strictEqual(corkedWriteCount, 1);

let sawNeedDrainDuringTransform = false;
let sawNeedDrainWriteCallback = false;
const backpressureTransform = new stream.Transform({
  highWaterMark: 1,
  transform(chunk, encoding, cb) {
    process.nextTick(() => {
      sawNeedDrainDuringTransform = true;
      assert.strictEqual(backpressureTransform._writableState.needDrain, true);
      assert.strictEqual(backpressureTransform.writableNeedDrain, true);
      cb();
    });
  }
});
assert.strictEqual(backpressureTransform._writableState.needDrain, false);
assert.strictEqual(backpressureTransform.writableLength, 0);
assert.strictEqual(backpressureTransform.write('large', () => {
  sawNeedDrainWriteCallback = true;
  assert.strictEqual(backpressureTransform._writableState.needDrain, false);
  assert.strictEqual(backpressureTransform.writableNeedDrain, false);
}), false);
assert.strictEqual(backpressureTransform._writableState.needDrain, true);
assert.strictEqual(backpressureTransform.writableLength, 5);

let sawDrainAfterEnd = false;
const noDrainAfterEnd = new stream.Writable({
  highWaterMark: 1,
  write(chunk, encoding, cb) {
    process.nextTick(cb);
  }
});
noDrainAfterEnd.on('drain', () => {
  sawDrainAfterEnd = true;
});
assert.strictEqual(noDrainAfterEnd.write('ending'), false);
noDrainAfterEnd.end();

let sawWritableError = false;
const errorWritable = new stream.Writable({
  write(chunk, encoding, cb) {
    cb(new Error('write failed'));
  }
});
assert.strictEqual(errorWritable.writable, true);
errorWritable.write('fail');
assert.strictEqual(errorWritable.writable, false);
errorWritable.on('error', () => {
  sawWritableError = true;
});

process.nextTick(() => process.nextTick(() => {
  assert(sawData);
  assert(sawEnd);
  assert(sawReadableListening);
  assert(sawDataReadable);
  assert(sawNeedDrainDuringTransform);
  assert(sawNeedDrainWriteCallback);
  assert.strictEqual(sawDrainAfterEnd, false);
  assert(sawWritableError);
  assert(sawPrefinish);
  assert(sawEndCallback);
  assert(sawFinish);
  console.log('stream state ok');
}));
