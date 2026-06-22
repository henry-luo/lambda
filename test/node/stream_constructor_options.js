const assert = require('assert');
const { Duplex, Readable } = require('stream');

{
  const duplex = new Duplex({
    objectMode: true,
    highWaterMark: 100
  });

  assert.strictEqual(duplex.writableObjectMode, true);
  assert.strictEqual(duplex.readableObjectMode, true);
  assert.strictEqual(duplex.writableHighWaterMark, 100);
  assert.strictEqual(duplex.readableHighWaterMark, 100);
  assert.strictEqual(duplex._writableState.objectMode, true);
  assert.strictEqual(duplex._readableState.objectMode, true);
}

{
  const duplex = new Duplex({
    readableObjectMode: false,
    readableHighWaterMark: 10,
    writableObjectMode: true,
    writableHighWaterMark: 100
  });

  assert.strictEqual(duplex.readableObjectMode, false);
  assert.strictEqual(duplex.readableHighWaterMark, 10);
  assert.strictEqual(duplex.writableObjectMode, true);
  assert.strictEqual(duplex.writableHighWaterMark, 100);
}

assert.throws(() => {
  new Readable({
    read() {},
    defaultEncoding: 'my invalid encoding'
  });
}, { code: 'ERR_UNKNOWN_ENCODING' });

console.log('stream constructor options ok');
