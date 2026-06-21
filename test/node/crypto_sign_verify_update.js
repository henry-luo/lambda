'use strict';

const assert = require('assert');
const crypto = require('crypto');

for (const factory of [
  () => crypto.createSign('sha256'),
  () => crypto.createVerify('SHA-256')
]) {
  for (const len of [15, 16]) {
    const obj = factory();
    assert.strictEqual(obj.update(Buffer.alloc(len), 'hex'), obj);
  }
}
console.log('sign verify buffer update encoding:', true);

{
  const verify = crypto.createVerify('sha1');
  assert.strictEqual(verify.update('Test'), verify);
  assert.strictEqual(verify.verify('not-a-real-key', 'YWJj', 'base64'), false);
  console.log('verify unsupported signature result:', false);
}
