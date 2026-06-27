const assert = require('assert');
const stream = require('stream');
const { addAbortSignalNoValidate } = require('internal/streams/add-abort-signal');

const readable = new stream.Readable({ read() {} });
assert.deepStrictEqual(readable.eventNames(), []);
readable.on('foo', () => {});
readable.on('data', () => {});
readable.on('error', () => {});
assert.deepStrictEqual(readable.eventNames(), ['error', 'data', 'foo']);

const writable = new stream.Writable({
  write(chunk, enc, cb) { cb(); }
});
assert.deepStrictEqual(writable.eventNames(), []);
writable.on('foo', () => {});
writable.on('drain', () => {});
writable.on('prefinish', () => {});
assert.deepStrictEqual(writable.eventNames(), ['prefinish', 'drain', 'foo']);

const ac = new AbortController();
assert.strictEqual(addAbortSignalNoValidate('bad', readable), readable);
assert.strictEqual(stream.addAbortSignal(ac.signal, readable), readable);
assert.throws(() => stream.addAbortSignal('bad', readable), { code: 'ERR_INVALID_ARG_TYPE' });
assert.throws(() => stream.addAbortSignal(ac.signal, 'bad'), { code: 'ERR_INVALID_ARG_TYPE' });

assert.strictEqual(typeof stream.isReadable, 'function');
assert.strictEqual(typeof stream.isWritable, 'function');
assert.strictEqual(typeof stream.isDestroyed, 'function');
assert.strictEqual(stream.isReadable(readable), true);
assert.strictEqual(stream.isWritable(readable), null);
assert.strictEqual(stream.isDestroyed(readable), false);
assert.strictEqual(stream.isReadable(writable), null);
assert.strictEqual(stream.isWritable(writable), true);
assert.strictEqual(stream.isDestroyed(writable), false);
assert.strictEqual(stream.isReadable({}), null);
assert.strictEqual(stream.isWritable({}), null);
assert.strictEqual(stream.isDestroyed({}), null);

writable.end();
assert.strictEqual(stream.isWritable(writable), false);

const destroyedReadable = new stream.Readable({ read() {} });
destroyedReadable.destroy();
assert.strictEqual(stream.isReadable(destroyedReadable), false);
assert.strictEqual(stream.isDestroyed(destroyedReadable), true);

console.log('stream surface ok');
