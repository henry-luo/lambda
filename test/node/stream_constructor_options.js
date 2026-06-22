const assert = require('assert');
const { Duplex, Readable, Writable } = require('stream');

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

class InspectWritable extends Writable {
  constructor(fn, options) {
    super(options);
    this.fn = fn;
  }

  _write(chunk, encoding, callback) {
    this.fn(Buffer.isBuffer(chunk), typeof chunk, encoding);
    callback();
  }
}

{
  const writable = new InspectWritable((isBuffer, type, encoding) => {
    assert.strictEqual(isBuffer, true);
    assert.strictEqual(type, 'object');
    assert.strictEqual(encoding, 'buffer');
  }, { decodeStrings: true });
  writable.write('some-text', 'utf8');
}

{
  const writable = new InspectWritable((isBuffer, type, encoding) => {
    assert.strictEqual(isBuffer, false);
    assert.strictEqual(type, 'string');
    assert.strictEqual(encoding, 'utf8');
  }, { decodeStrings: false });
  writable.write('some-text', 'utf8');
}

{
  const writable = new InspectWritable((isBuffer, type, encoding) => {
    assert.strictEqual(isBuffer, false);
    assert.strictEqual(type, 'string');
    assert.strictEqual(encoding, 'hex');
  }, { defaultEncoding: 'hex', decodeStrings: false });
  writable.write('asd');
}

{
  const writable = new InspectWritable((isBuffer, type, encoding) => {
    assert.strictEqual(isBuffer, false);
    assert.strictEqual(type, 'string');
    assert.strictEqual(encoding, 'ascii');
  }, { decodeStrings: false });
  writable.setDefaultEncoding('AsCii');
  writable.write('asd');
}

assert.throws(() => {
  const writable = new InspectWritable(() => {}, { decodeStrings: false });
  writable.setDefaultEncoding({});
}, {
  name: 'TypeError',
  code: 'ERR_UNKNOWN_ENCODING',
  message: 'Unknown encoding: {}'
});

{
  const writable = new InspectWritable((isBuffer, type, encoding) => {
    assert.strictEqual(isBuffer, false);
    assert.strictEqual(type, 'string');
    assert.strictEqual(encoding, 'utf8');
  }, { defaultEncoding: null, decodeStrings: false });
  writable.write('asd');
}

{
  const writable = new InspectWritable((isBuffer, type, encoding) => {
    assert.strictEqual(isBuffer, false);
    assert.strictEqual(type, 'object');
    assert.strictEqual(encoding, 'utf8');
  }, { defaultEncoding: 'hex', objectMode: true });
  writable.write({ foo: 'bar' }, 'utf8');
}

console.log('stream constructor options ok');
