const assert = require('assert');
const { Writable, finished } = require('stream');

const writable = new Writable({
  write(chunk, encoding, callback) {
    callback();
  }
});

assert.throws(
  () => finished(writable, 'bad-callback'),
  { code: 'ERR_INVALID_ARG_TYPE' }
);

finished(writable, (err) => {
  assert.ifError(err);
  console.log('stream finished after invalid args ok');
});

writable.end('done');
