const assert = require('assert');
const stream = require('stream');
const streamPromises = require('stream/promises');
const { promisify } = require('util');

(async () => {
  assert.strictEqual(stream.promises, streamPromises);
  assert.strictEqual(stream.promises.pipeline, streamPromises.pipeline);
  assert.strictEqual(stream.promises.finished, streamPromises.finished);
  assert.strictEqual(promisify(stream.pipeline), streamPromises.pipeline);
  assert.strictEqual(promisify(stream.finished), streamPromises.finished);

  const seen = [];
  const readable = new stream.Readable({ read() {} });
  const writable = new stream.Writable({
    write(chunk, enc, callback) {
      seen.push(chunk.toString());
      callback();
    }
  });
  readable.push('a');
  readable.push('b');
  readable.push(null);
  await streamPromises.pipeline(readable, writable);
  assert.deepStrictEqual(seen, ['a', 'b']);

  const cleanupStream = new stream.Writable();
  const cleanupPromise = streamPromises.finished(cleanupStream, { cleanup: true });
  cleanupStream.end();
  await cleanupPromise;
  assert.strictEqual(cleanupStream.listenerCount('end'), 0);

  const keepStream = new stream.Writable();
  const keepPromise = streamPromises.finished(keepStream, { cleanup: false });
  keepStream.end();
  await keepPromise;
  assert.strictEqual(keepStream.listenerCount('end'), 1);

  assert.throws(() => {
    streamPromises.finished(new stream.Readable(), { cleanup: 2 });
  }, { code: 'ERR_INVALID_ARG_TYPE' });

  console.log('stream promises ok');
})().catch((err) => {
  console.log(err && err.stack || err);
  process.exit(1);
});
