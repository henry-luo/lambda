const assert = require('assert');
const {
  Duplex,
  Readable,
  Transform,
  Writable,
  getDefaultHighWaterMark,
  setDefaultHighWaterMark
} = require('stream');

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

{
  assert.strictEqual(getDefaultHighWaterMark(false), 16 * 1024);
  assert.strictEqual(getDefaultHighWaterMark(true), 16);
  setDefaultHighWaterMark(false, 32 * 1000);
  setDefaultHighWaterMark(true, 32);

  const writable = new Writable({ write() {} });
  const readable = new Readable({ read() {} });
  const transform = new Transform({ transform() {} });
  assert.strictEqual(writable.writableHighWaterMark, 32 * 1000);
  assert.strictEqual(readable.readableHighWaterMark, 32 * 1000);
  assert.strictEqual(transform.writableHighWaterMark, 32 * 1000);
  assert.strictEqual(transform.readableHighWaterMark, 32 * 1000);
  assert.strictEqual(getDefaultHighWaterMark(false), 32 * 1000);
  assert.strictEqual(getDefaultHighWaterMark(true), 32);

  setDefaultHighWaterMark(false, 16 * 1024);
  setDefaultHighWaterMark(true, 16);
}

{
  const defaultHwm = getDefaultHighWaterMark(false);
  const readableSplit = new Transform({ readableHighWaterMark: 123 });
  const writableSplit = new Transform({ writableHighWaterMark: 456 });
  const genericOverride = new Transform({
    highWaterMark: 789,
    readableHighWaterMark: 123,
    writableHighWaterMark: 456
  });
  assert.strictEqual(readableSplit.readableHighWaterMark, 123);
  assert.strictEqual(readableSplit.writableHighWaterMark, defaultHwm);
  assert.strictEqual(writableSplit.readableHighWaterMark, defaultHwm);
  assert.strictEqual(writableSplit.writableHighWaterMark, 456);
  assert.strictEqual(genericOverride.readableHighWaterMark, 789);
  assert.strictEqual(genericOverride.writableHighWaterMark, 789);
  assert.strictEqual(new Readable({ readableHighWaterMark: 123, read() {} }).readableHighWaterMark, defaultHwm);
  assert.strictEqual(new Writable({ writableHighWaterMark: 456, write() {} }).writableHighWaterMark, defaultHwm);
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

{
  const writable = new Writable({ write() {} });
  assert.throws(() => writable.write({}), { code: 'ERR_INVALID_ARG_TYPE' });
  assert.throws(() => writable.write(false), { code: 'ERR_INVALID_ARG_TYPE' });
  assert.throws(() => writable.write(undefined), { code: 'ERR_INVALID_ARG_TYPE' });
  assert.throws(() => writable.write(null), { code: 'ERR_STREAM_NULL_VALUES' });
}

{
  const writable = new Writable({ objectMode: true, write(chunk, encoding, cb) { cb(); } });
  writable.write({});
  writable.write(false);
  writable.write(undefined);
  assert.throws(() => writable.write(null), { code: 'ERR_STREAM_NULL_VALUES' });
}

console.log('stream constructor options ok');
