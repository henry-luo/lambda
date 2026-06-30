const assert = require('assert');
const { Stream, Readable, Writable, pipeline } = require('stream');

const legacy = new Stream();
legacy.pause = legacy.resume = () => {};
legacy.write = (chunk) => {
  legacy.emit('data', chunk);
  return true;
};
legacy.end = () => {
  legacy.emit('end');
};

const expected = ['hello', 'world'];
const read = new Readable({
  read() {
    for (const chunk of expected) this.push(chunk);
    this.push(null);
  }
});

let actual = '';
const write = new Writable({
  write(chunk, enc, cb) {
    actual += chunk;
    cb();
  }
});

pipeline(read, legacy, write, (err) => {
  assert.ifError(err);
  assert.strictEqual(actual, expected.join(''));
  console.log('stream pipeline legacy stream ok');
});
