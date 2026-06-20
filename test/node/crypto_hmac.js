const assert = require('assert');
const crypto = require('crypto');
const { Buffer } = require('buffer');

const ctor = crypto.Hmac('sha256', 'Node');
console.log('hmac instanceof:', ctor instanceof crypto.Hmac);
ctor.digest('hex');

assert.throws(() => crypto.createHmac(null), {
  code: 'ERR_INVALID_ARG_TYPE',
  message: 'The "hmac" argument must be of type string. Received null'
});
console.log('hmac invalid arg:', true);

let boom = false;
try {
  crypto.createHmac('sha256', 'key').digest({
    toString() { throw new Error('boom'); }
  });
} catch (err) {
  boom = err && err.name === 'Error' && err.message === 'boom';
}
console.log('hmac encoding throws:', boom);

assert.throws(() => crypto.createHmac('sha1', null), {
  code: 'ERR_INVALID_ARG_TYPE',
  name: 'TypeError'
});
console.log('hmac key invalid:', true);

console.log('hmac md5 vector:',
  crypto.createHmac('md5', 'key')
    .update('The quick brown fox jumps over the lazy dog')
    .digest('hex') === '80070713463e7749b90c2dc24911e275');

console.log('hmac sha1 multi:',
  crypto.createHmac('sha1', 'Node')
    .update('some data')
    .update('to hmac')
    .digest('hex') === '19fd6e1ba73d9ed2224dd5094a71babe85d9a892');

console.log('hmac sha224 vector:',
  crypto.createHmac('sha224', Buffer.from('0b'.repeat(20), 'hex'))
    .update(Buffer.from('4869205468657265', 'hex'))
    .digest('hex') === '896fb1128abbdf196832107cd49df33f47b4b1169912ba4f53684b22');

const raw = crypto.createHmac('sha1', 'key').update('data').digest('buffer');
console.log('hmac raw buffer:', Buffer.isBuffer(raw));
console.log('hmac raw hex:', raw.toString('hex') === '104152c5bfdca07bc633eebd46199f0255c9f49d');

const latin1 = crypto.createHmac('sha1', 'key').update('data').digest('latin1');
console.log('hmac latin1:', Buffer.from(latin1, 'latin1').toString('hex') === raw.toString('hex'));

const h = crypto.createHmac('sha1', 'key').update('data');
h.digest('buffer');
console.log('hmac second digest:', h.digest('buffer').length === 0);

const stream = crypto.createHmac('sha256', Buffer.from('key'));
stream.end(Buffer.from('data'));
console.log('hmac end/read:',
  stream.read().toString('hex') ===
    crypto.createHmac('sha256', 'key').update('data').digest('hex'));

const emptyKey = Buffer.alloc(0);
const keyObject = crypto.createSecretKey(emptyKey);
console.log('hmac key object:',
  crypto.createHmac('sha256', emptyKey).update('foo').digest().toString('hex') ===
    crypto.createHmac('sha256', keyObject).update('foo').digest().toString('hex'));

assert.throws(() => crypto.createHmac('sha7', 'key'), /Invalid digest/);
console.log('hmac invalid digest:', true);
