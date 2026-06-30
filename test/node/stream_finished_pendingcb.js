const assert = require('assert');
const { Duplex, finished } = require('stream');

let callbacks = 0;

const stream = new Duplex({
  read() {},
  write(chunk, enc, cb) {
    setImmediate(cb);
  }
});

stream.end('foo');
assert.strictEqual(stream._writableState.pendingcb, 1);

finished(stream, { readable: false }, (err) => {
  callbacks++;
  assert.strictEqual(err, undefined);
  assert.strictEqual(stream._writableState.pendingcb, 0);

  const legacy = new Duplex({
    read() {},
    write(chunk, enc, cb) {}
  });

  legacy.end('foo');
  delete legacy._writableState.pendingcb;

  finished(legacy, { readable: false }, (legacyErr) => {
    callbacks++;
    assert.strictEqual(legacyErr, undefined);
    assert.strictEqual(callbacks, 2);
    console.log('stream finished pendingcb ok');
  });
});
