const assert = require('assert');
const { Readable } = require('stream');

(async () => {
  const readable = new Readable({ read() {} });
  const iterator = readable[Symbol.asyncIterator]();

  const pending = iterator.next();
  let observed = false;
  pending.then((result) => {
    observed = true;
    assert.strictEqual(result.done, false);
    assert.strictEqual(Buffer.isBuffer(result.value), true);
    assert.strictEqual(result.value.toString(), 'late');
  });

  process.nextTick(() => {
    readable.push('late');
    readable.push(null);
  });

  const first = await pending;
  assert.strictEqual(first.done, false);
  assert.strictEqual(first.value.toString(), 'late');
  assert.strictEqual(observed, true);

  const done = await iterator.next();
  assert.deepStrictEqual(done, { value: undefined, done: true });

  const returned = await iterator.return();
  assert.deepStrictEqual(returned, { value: undefined, done: true });

  console.log('stream async iterator pending ok');
})().catch((err) => {
  console.log(err && err.stack || err);
  process.exit(1);
});
