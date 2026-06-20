const assert = require('assert');
const crypto = require('crypto');
const { Buffer } = require('buffer');

const sync = crypto.pbkdf2Sync('password', 'salt', 1, 20, 'sha256');
console.log('pbkdf2 buffer:', Buffer.isBuffer(sync));
console.log('pbkdf2 latin1:', sync.toString('latin1') ===
  '\x12\x0f\xb6\xcf\xfc\xf8\xb3\x2c\x43\xe7\x22\x52\x56\xc4\xf8\x37\xa8\x65\x48\xc9');
console.log('pbkdf2 hex:', sync.toString('hex') === '120fb6cffcf8b32c43e7225256c4f837a86548c9');

crypto.pbkdf2('password', 'salt', 2, 20, 'sha256', (err, derived) => {
  console.log('pbkdf2 async err:', err === null);
  console.log('pbkdf2 async buffer:', Buffer.isBuffer(derived));
  console.log('pbkdf2 async hex:', derived.toString('hex') === 'ae4d0c95af6b46d32d0adff928f06dd02a303f8e');
});

assert.throws(() => crypto.pbkdf2Sync('pass', 'salt', 8, 8, 'md55'), {
  code: 'ERR_CRYPTO_INVALID_DIGEST',
  message: 'Invalid digest: md55'
});
console.log('invalid digest code:', 'ERR_CRYPTO_INVALID_DIGEST');

assert.throws(() => crypto.pbkdf2('password', 'salt', 8, 8, () => {}), {
  code: 'ERR_INVALID_ARG_TYPE',
  message: 'The "digest" argument must be of type string. Received undefined'
});
console.log('missing digest overload:', true);

assert.throws(() => crypto.pbkdf2('password', 'salt', 1, Infinity, 'sha256', () => {}), {
  code: 'ERR_OUT_OF_RANGE',
  message: 'The value of "keylen" is out of range. It must be an integer. Received Infinity'
});
console.log('keylen infinity:', true);

console.log('buffer latin1 high:', Buffer.from([0xae, 0x4d]).toString('latin1') === '\xaeM');
