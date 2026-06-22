const assert = require('assert');
const { Duplex } = require('stream');

const noReadable = new Duplex({ readable: false });
assert.strictEqual(noReadable.readable, false);
assert.strictEqual(noReadable.push('chunk'), false);
noReadable.on('data', () => assert.fail('disabled readable emitted data'));
noReadable.on('end', () => assert.fail('disabled readable emitted end'));
const readError = new Promise((resolve) => {
  noReadable.on('error', (err) => {
    assert.strictEqual(err.code, 'ERR_STREAM_PUSH_AFTER_EOF');
    resolve();
  });
});

let writeCalled = false;
const noWritable = new Duplex({
  writable: false,
  write() {
    writeCalled = true;
  }
});
assert.strictEqual(noWritable.writable, false);
assert.strictEqual(noWritable.write('chunk'), false);
noWritable.on('finish', () => assert.fail('disabled writable emitted finish'));
const writeError = new Promise((resolve) => {
  noWritable.on('error', (err) => {
    assert.strictEqual(err.code, 'ERR_STREAM_WRITE_AFTER_END');
    resolve();
  });
});

const noReadableIter = new Duplex({ readable: false });
const iterDone = (async () => {
  for await (const chunk of noReadableIter) {
    assert.fail(`disabled readable yielded ${chunk}`);
  }
})();

Promise.all([readError, writeError, iterDone]).then(() => {
  assert.strictEqual(writeCalled, false);
  console.log('stream duplex disabled ok');
});
