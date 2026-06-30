const assert = require('assert');
const { Duplex, Readable, Writable, finished } = require('stream');

let callbacks = 0;

const readable = new Readable({ read() {} });
readable.destroy();
finished(readable, (err) => {
  callbacks++;
  assert.strictEqual(err.code, 'ERR_STREAM_PREMATURE_CLOSE');
});

const writable = new Writable({ write() {} });
writable.destroy();
finished(writable, (err) => {
  callbacks++;
  assert.strictEqual(err.code, 'ERR_STREAM_PREMATURE_CLOSE');
});

const duplex = new Duplex({
  readable: false,
  write(chunk, encoding, callback) {
    callback();
  }
});
duplex.destroy();
finished(duplex, { readable: false, writable: true }, (err) => {
  callbacks++;
  assert.strictEqual(err.code, 'ERR_STREAM_PREMATURE_CLOSE');
});

let destroyCompleted = false;
const asyncDestroy = new Writable({
  write(chunk, encoding, callback) {
    callback();
  },
  destroy(err, callback) {
    setImmediate(() => {
      destroyCompleted = true;
      callback();
    });
  }
});
asyncDestroy.destroy();
finished(asyncDestroy, (err) => {
  callbacks++;
  assert.strictEqual(destroyCompleted, true);
  assert.strictEqual(err.code, 'ERR_STREAM_PREMATURE_CLOSE');
});

const pendingEnd = new Readable({ read() {} });
finished(pendingEnd, (err) => {
  callbacks++;
  assert.strictEqual(err.code, 'ERR_STREAM_PREMATURE_CLOSE');
});
pendingEnd.push('chunk');
pendingEnd.push(null);
pendingEnd.destroy();

setTimeout(() => {
  assert.strictEqual(callbacks, 5);
  console.log('stream finished destroyed premature ok');
}, 5);
