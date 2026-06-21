'use strict';

const assert = require('assert');
const crypto = require('crypto');
const Buffer = require('buffer');

const desKey = '0123456789abcd0123456789';
const desIv = '12345678';
const plaintext = 'Node crypto cipher compatibility';

const cipher = crypto.createCipheriv('des-ede3-cbc', desKey, desIv);
let encryptedHex = cipher.update(plaintext, 'utf8', 'hex');
encryptedHex += cipher.final('hex');

const decipher = crypto.createDecipheriv('des-ede3-cbc', desKey, desIv);
let decrypted = decipher.update(encryptedHex, 'hex', 'utf8');
decrypted += decipher.final('utf8');
console.log('des-ede3-cbc roundtrip:', decrypted === plaintext);

const cStream = crypto.createCipheriv('des-ede3-cbc', desKey, desIv);
cStream.end(plaintext);
const streamedCiphertext = cStream.read(cStream.readableLength);
const dStream = crypto.createDecipheriv('des-ede3-cbc', desKey, desIv);
dStream.end(streamedCiphertext);
console.log('des-ede3-cbc stream:', dStream.read(dStream.readableLength).toString('utf8') === plaintext);

console.log('cipheriv instanceof:', crypto.createCipheriv('des-ede3-cbc', desKey, desIv) instanceof crypto.Cipheriv);
console.log('decipheriv instanceof:', crypto.createDecipheriv('des-ede3-cbc', desKey, desIv) instanceof crypto.Decipheriv);
console.log('rsa-sha sign ctor:', crypto.createSign('RSA-SHA1') instanceof crypto.Sign);
console.log('rsa-sha verify ctor:', crypto.createVerify('RSA-SHA1') instanceof crypto.Verify);

const kwKey = Buffer.from('000102030405060708090A0B0C0D0E0F', 'hex');
const kwIv = Buffer.from('A6A6A6A6A6A6A6A6', 'hex');
const kwPlaintext = Buffer.from('00112233445566778899AABBCCDDEEFF', 'hex');
const kwCipher = crypto.createCipheriv('id-aes128-wrap', kwKey, kwIv);
const wrapped = Buffer.concat([kwCipher.update(kwPlaintext, 'utf8', 'buffer'), kwCipher.final('buffer')]);
console.log('aes kw vector:', wrapped.toString('hex') === '1fa68b0a8112b447aef34bd8fb5a7b829d3e862371d2cfe5');

const kwDecipher = crypto.createDecipheriv('id-aes128-wrap', kwKey, kwIv);
const unwrapped = Buffer.concat([kwDecipher.update(wrapped, 'buffer'), kwDecipher.final()]);
console.log('aes kw unwrap:', unwrapped.equals(kwPlaintext));

crypto.createCipheriv('aes-128-ecb', Buffer.alloc(16), Buffer.alloc(0));
crypto.createCipheriv('aes-128-ecb', Buffer.alloc(16), null);
assert.throws(() => crypto.createCipheriv('aes-128-ecb', Buffer.alloc(16), Buffer.alloc(1)), /Invalid initialization vector/);
assert.throws(() => crypto.createCipheriv('aes-127', Buffer.alloc(16), null), { code: 'ERR_CRYPTO_UNKNOWN_CIPHER' });
assert.throws(() => crypto.createCipheriv('aes-128-ecb', Buffer.alloc(17), null), /Invalid key length/);
assert.throws(() => Buffer.allocUnsafeSlow(2 ** 31 - 1));
console.log('cipher validation:', true);
