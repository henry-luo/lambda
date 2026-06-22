const assert = require('assert');
const { Readable, Writable } = require('stream');

const abc = new Uint8Array([0x41, 0x42, 0x43]);
const def = new Uint8Array([0x44, 0x45, 0x46]);
const ghi = new Uint8Array([0x47, 0x48, 0x49]);
const xyz = new Uint8Array([0x58, 0x59, 0x5a]);
const xyzView = new DataView(xyz.buffer, xyz.byteOffset, xyz.byteLength);

assert.strictEqual(Buffer.isBuffer(Buffer.from('ok')), true);
assert.strictEqual(Buffer.isBuffer(abc), false);
assert.deepStrictEqual(new DataView(xyz.buffer), new DataView(Buffer.from('XYZ').buffer));

{
  let writes = 0;
  const writable = new Writable({
    write(chunk, encoding, cb) {
      assert.strictEqual(Buffer.isBuffer(chunk), true);
      assert.strictEqual(chunk instanceof Buffer, true);
      assert.strictEqual(encoding, 'buffer');
      assert.strictEqual(String(chunk), writes++ === 0 ? 'ABC' : 'DEF');
      cb();
    }
  });
  writable.write(abc);
  writable.end(def);
}

{
  const writable = new Writable({
    objectMode: true,
    write(chunk, encoding, cb) {
      assert.strictEqual(Buffer.isBuffer(chunk), false);
      assert.strictEqual(chunk, abc);
      assert.strictEqual(encoding, undefined);
      cb();
    }
  });
  writable.end(abc);
}

{
  const writable = new Writable({
    write(chunk, encoding, cb) {
      assert.strictEqual(Buffer.isBuffer(chunk), true);
      assert.strictEqual(chunk instanceof Buffer, true);
      assert.strictEqual(encoding, 'buffer');
      assert.strictEqual(String(chunk), 'XYZ');
      cb();
    }
  });
  writable.end(xyzView);
}

{
  let firstCallback;
  const writable = new Writable({
    write(chunk, encoding, cb) {
      assert.strictEqual(Buffer.isBuffer(chunk), true);
      assert.strictEqual(String(chunk), 'ABC');
      assert.strictEqual(encoding, 'buffer');
      firstCallback = cb;
    },
    writev(chunks, cb) {
      assert.strictEqual(chunks.length, 2);
      assert.strictEqual(chunks[0].encoding, 'buffer');
      assert.strictEqual(chunks[1].encoding, 'buffer');
      assert.strictEqual(String(chunks[0].chunk) + String(chunks[1].chunk), 'DEFGHI');
      cb();
    }
  });
  writable.write(abc);
  writable.write(def);
  writable.end(ghi);
  firstCallback();
}

{
  const readable = new Readable({ read() {} });
  readable.push(def);
  readable.unshift(abc);
  assert.strictEqual(Buffer.isBuffer(readable.read()), true);
  assert.strictEqual(String(readable.read()), 'DEF');
}

{
  const readable = new Readable({ read() {} });
  readable.setEncoding('utf8');
  readable.push(def);
  readable.unshift(abc);
  assert.strictEqual(readable.read(), 'ABCDEF');
}

console.log('stream typedarray ok');
