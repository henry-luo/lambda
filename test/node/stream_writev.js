const assert = require('assert');
const { Writable } = require('stream');

let batchedOrder = [];
const batched = new Writable({ decodeStrings: true });
batched._write = () => {
  throw new Error('_write should not be called for batched writes');
};
batched._writev = (chunks, cb) => {
  assert.strictEqual(chunks.length, 2);
  assert.deepStrictEqual(chunks.map(({ encoding }) => encoding), ['buffer', 'buffer']);
  batchedOrder.push('writev');
  cb();
};

batched.cork();
batched.write('a', () => batchedOrder.push('a'));
batched.write(Buffer.from('b'), () => batchedOrder.push('b'));
batched.uncork();
batched.end(() => batchedOrder.push('end'));
batched.on('finish', () => {
  batchedOrder.push('finish');
  assert.deepStrictEqual(batchedOrder, ['writev', 'a', 'b', 'end', 'finish']);
});

let singleWritev = false;
let singleCallback = false;
const single = new Writable({
  writev(chunks, cb) {
    assert.strictEqual(chunks.length, 1);
    assert.strictEqual(chunks[0].chunk.toString(), 'x');
    singleWritev = true;
    cb();
  }
});
single.write('x', () => {
  singleCallback = true;
});

process.on('exit', () => {
  assert.strictEqual(singleWritev, true);
  assert.strictEqual(singleCallback, true);
  console.log('stream writev ok');
});
