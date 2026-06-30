const assert = require('assert');
const { PassThrough } = require('stream');

const target = new PassThrough();
const chunk = Buffer.allocUnsafe(1000);
let writes = 0;

while (target.write(chunk)) {
  writes++;
  assert(writes < 100, 'PassThrough.write() did not apply readable backpressure');
}

assert.strictEqual(target._writableState.needDrain, true);
assert.strictEqual(target.writableNeedDrain, true);

let drained = false;
target.on('drain', () => {
  drained = true;
  assert.strictEqual(target._writableState.needDrain, false);
  assert.strictEqual(target.writableNeedDrain, false);
  console.log('stream transform readable backpressure ok');
});

target.on('data', () => {});

process.on('beforeExit', () => {
  assert.strictEqual(drained, true);
});
