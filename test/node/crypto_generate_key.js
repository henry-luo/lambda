const crypto = require('crypto');

const aes128 = crypto.generateKeySync('aes', { length: 128 });
console.log('aes type:', aes128.type);
console.log('aes size:', aes128.symmetricKeySize);
console.log('aes export len:', aes128.export().byteLength);

const hmac123 = crypto.generateKeySync('hmac', { length: 123 });
console.log('hmac size:', hmac123.symmetricKeySize);
console.log('hmac export len:', hmac123.export().byteLength);

try {
  crypto.generateKeySync('aes', { length: 123 });
  console.log('aes invalid: false');
} catch (err) {
  console.log('aes invalid:', err.code);
}

let afterCall = false;
crypto.generateKey('aes', { length: 256 }, function(err, key) {
  console.log('async err:', err === null);
  console.log('async order:', afterCall);
  console.log('async size:', key.symmetricKeySize);
  console.log('async export len:', key.export().byteLength);
});
afterCall = true;
