const assert = require('assert');
const { Writable, pipeline } = require('stream');

let result = '';

pipeline(function*() {
  yield 'hello';
  yield 'world';
}, new Writable({
  write(chunk, encoding, callback) {
    result += chunk;
    callback();
  }
}), (err) => {
  assert.ifError(err);
  assert.strictEqual(result, 'helloworld');
  console.log('stream pipeline function source ok');
});
