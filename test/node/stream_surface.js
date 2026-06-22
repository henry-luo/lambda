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

console.log('stream surface ok');
