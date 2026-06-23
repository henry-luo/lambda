const assert = require('assert');
const { Readable, finished } = require('stream');
const { AsyncLocalStorage } = require('async_hooks');

const als = new AsyncLocalStorage();
const readable = new Readable();
let called = 0;

als.run('stream-finished-context', () => {
  assert.strictEqual(als.getStore(), 'stream-finished-context');
  finished(readable, () => {
    called++;
    assert.strictEqual(als.getStore(), 'stream-finished-context');
  });
});

assert.strictEqual(als.getStore(), undefined);
readable.destroy();

process.on('exit', () => {
  assert.strictEqual(called, 1);
  assert.strictEqual(als.getStore(), undefined);
  console.log('stream finished async local storage ok');
});
