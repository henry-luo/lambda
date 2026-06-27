const assert = require('assert');
const { Readable, Transform } = require('stream');

(async () => {
  const transformed = Readable.from(['a', 'b']).compose(new Transform({
    objectMode: true,
    transform(chunk, encoding, callback) {
      callback(null, chunk + chunk);
    }
  }));

  assert.strictEqual(transformed.readable, true);
  assert.strictEqual(transformed.writable, false);
  assert.deepStrictEqual(await transformed.toArray(), ['aa', 'bb']);

  const nested = Readable.from(['hello'])
    .compose(async function* (source) {
      for await (const chunk of source) {
        throw new Error(`boom: ${chunk}`);
      }
    })
    .compose(async function* (source) {
      yield* source;
    });

  await assert.rejects(nested.toArray(), /boom: hello/);

  console.log('stream compose transform nested ok');
})().catch((err) => {
  console.log(err && err.stack || err);
  process.exit(1);
});
