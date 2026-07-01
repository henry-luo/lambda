const assert = require('assert');
const { Stream, pipeline } = require('stream');

const source = new Stream();
let destroyCalls = 0;

source.destroy = () => {
  destroyCalls++;
};

pipeline(source, async function(stream) {
  for await (const chunk of stream) {
  }
}, (err) => {
  assert.strictEqual(err && err.message, 'kaboom');
  assert.strictEqual(destroyCalls, 1);
  console.log('stream pipeline legacy error destroy ok');
});

process.nextTick(() => {
  source.emit('error', new Error('kaboom'));
});
