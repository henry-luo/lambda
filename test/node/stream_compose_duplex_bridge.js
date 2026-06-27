const assert = require('assert');
const { PassThrough, Readable } = require('stream');

(async () => {
  const readableOnly = Readable.from(['a', 'b']).compose(async function* (source) {
    for await (const chunk of source) {
      yield chunk + chunk;
    }
  });
  assert.strictEqual(readableOnly.readable, true);
  assert.strictEqual(readableOnly.writable, false);
  assert.deepStrictEqual(await readableOnly.toArray(), ['aa', 'bb']);

  const head = new PassThrough({ objectMode: true });
  const composed = head.compose(async function* (source) {
    for await (const chunk of source) {
      yield chunk * 2;
    }
  });

  const headSeen = [];
  head.on('data', (chunk) => headSeen.push(chunk));

  assert.strictEqual(composed.readable, true);
  assert.strictEqual(composed.writable, true);
  assert.strictEqual(typeof composed.write, 'function');
  assert.strictEqual(typeof composed.end, 'function');

  composed.end(21);

  assert.deepStrictEqual(await composed.toArray(), [42]);
  assert.deepStrictEqual(headSeen, [21]);

  console.log('stream compose duplex bridge ok');
})().catch((err) => {
  console.log(err && err.stack || err);
  process.exit(1);
});
