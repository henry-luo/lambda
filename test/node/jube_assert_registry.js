const assert = require('assert');
const strict = require('assert/strict');

console.log(assert === require('node:assert'), strict === require('node:assert/strict'));
console.log(strict === assert.strict, typeof assert.equal, typeof strict.deepEqual);
assert.equal(4, 4);
strict.deepEqual([1, 2], [1, 2]);
console.log('ok');
