'use strict';

const crypto = require('crypto');

const ikm = Buffer.from('0b'.repeat(22), 'hex');
const salt = Buffer.from('000102030405060708090a0b0c', 'hex');
const info = Buffer.from('f0f1f2f3f4f5f6f7f8f9', 'hex');
const expected = '3cb25f25faacd57a90434f64d0362f2a' +
                 '2d2d0a90cf1a5a4c5db02d56ecc4c5bf' +
                 '34007208d5b887185865';

const sync = crypto.hkdfSync('sha256', ikm, salt, info, 42);
console.log('hkdf arraybuffer:', sync instanceof ArrayBuffer);
console.log('hkdf vector:', Buffer.from(sync).toString('hex') === expected);

const key = crypto.createSecretKey(ikm);
const keyed = crypto.hkdfSync('sha256', key, salt, info, 42);
console.log('secret key size:', key.symmetricKeySize);
console.log('secret key hkdf:', Buffer.from(keyed).toString('hex') === expected);

let afterCall = false;
crypto.hkdf('sha256', ikm, salt, info, 16, function(err, derived) {
  console.log('hkdf async err:', err === null);
  console.log('hkdf async order:', afterCall);
  console.log('hkdf async len:', derived.byteLength);
});
afterCall = true;
