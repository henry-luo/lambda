const assert = require('assert');
const { Writable, finished } = require('stream');

let callbackType;
const writable = new Writable({
  write(chunk, encoding, callback) {
    callbackType = typeof callback;
    setImmediate(callback);
  }
});

finished(writable, (err) => {
  assert.strictEqual(callbackType, 'function');
  assert.strictEqual(err && err.code, 'ERR_STREAM_PREMATURE_CLOSE');
  console.log('stream finished destroy pending ok');
});

writable.end('chunk');
writable.destroy();
