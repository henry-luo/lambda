const assert = require('assert');
const { PassThrough, pipeline } = require('stream');

let collected = '';
pipeline(async function*() {
  yield 'hello';
  yield 'world';
}, async function*(source) {
  for await (const chunk of source) {
    yield chunk.toUpperCase();
  }
}, async function(source) {
  let ret = '';
  for await (const chunk of source) {
    ret += chunk;
  }
  return ret;
}, (err, value) => {
  assert.ifError(err);
  assert.strictEqual(value, 'HELLOWORLD');
  collected = value;
});

const returned = pipeline(async function*() {
  yield 'done';
}, async function*(source) {
  for await (const chunk of source) {
    yield chunk;
  }
}, (err) => {
  assert.ifError(err);
  assert.strictEqual(collected, 'HELLOWORLD');
});
returned.resume();

{
  const stream = new PassThrough();
  assert.throws(() => {
    pipeline(function() {}, stream, () => {});
  }, { code: 'ERR_INVALID_RETURN_VALUE' });
  assert.strictEqual(stream.destroyed, false);
}

{
  const stream = new PassThrough();
  pipeline(async function*() {
    yield 'x';
  }, stream, async function(source) {
    for await (const chunk of source) {
      throw new Error('sink boom');
    }
  }, (err) => {
    assert.strictEqual(err.message, 'sink boom');
    assert.strictEqual(stream.destroyed, true);
    console.log('stream pipeline function stages ok');
  });
}
