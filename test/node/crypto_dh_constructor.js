'use strict';

const crypto = require('crypto');

const dh = crypto.createDiffieHellman(1024);
const prime = dh.getPrime('buffer');

console.log('dh instanceof:', dh instanceof crypto.DiffieHellman);
console.log('dh prime bytes:', prime.length >= 128);
console.log('dh generator hex:', dh.getGenerator('hex'));

const dhCtor = crypto.DiffieHellman(prime, 'buffer');
console.log('dh ctor instanceof:', dhCtor instanceof crypto.DiffieHellman);

const group = crypto.createDiffieHellmanGroup('modp5');
console.log('dh group instanceof:', group instanceof crypto.DiffieHellmanGroup);
console.log('dh group prime bytes:', group.getPrime().length >= 192);

const groupCtor = crypto.DiffieHellmanGroup('modp5');
console.log('dh group ctor instanceof:', groupCtor instanceof crypto.DiffieHellmanGroup);

const alice = crypto.createDiffieHellmanGroup('modp2');
const bob = crypto.createDiffieHellman(alice.getPrime(), alice.getGenerator());
const alicePub = alice.generateKeys();
const bobPubHex = bob.generateKeys('hex');
const aliceSecretHex = alice.computeSecret(bobPubHex, 'hex', 'hex');
const bobSecretHex = bob.computeSecret(alicePub).toString('hex');
console.log('dh secret match:', aliceSecretHex === bobSecretHex);

const restored = crypto.createDiffieHellman(alice.getPrime(), alice.getGenerator());
restored.setPrivateKey(alice.getPrivateKey());
console.log('dh restored public:', restored.getPublicKey('hex') === alicePub.toString('hex'));

const encodedGen = crypto.createDiffieHellman(alice.getPrime(), '02', 'hex');
encodedGen.generateKeys();
const encodedSecretHex = encodedGen.computeSecret(alicePub).toString('hex');
const aliceEncodedSecretHex = alice.computeSecret(encodedGen.getPublicKey()).toString('hex');
console.log('dh encoded generator:', encodedSecretHex === aliceEncodedSecretHex);

let ecbFinalError = null;
try {
  crypto.createDecipheriv('aes-128-ecb', crypto.randomBytes(16), '').final('utf8');
} catch (err) {
  ecbFinalError = err;
}
console.log('decipher empty final:', ecbFinalError &&
    ecbFinalError.code === 'ERR_OSSL_EVP_WRONG_FINAL_BLOCK_LENGTH' &&
    ecbFinalError.library === 'Cipher functions');

let noPaddingError = null;
try {
  const cipher = crypto.createCipheriv('aes-128-cbc',
                                       'S3c.r.e.t.K.e.Y!',
                                       'blahFizz2011Buzz');
  cipher.setAutoPadding(false);
  cipher.update('Hello node world!', 'ascii', 'hex');
  cipher.final('hex');
} catch (err) {
  noPaddingError = err;
}
console.log('cipher no padding error:', noPaddingError &&
    noPaddingError.code === 'ERR_OSSL_EVP_DATA_NOT_MULTIPLE_OF_BLOCK_LENGTH');

const ecdh = crypto.createECDH('prime256v1');
console.log('ecdh instanceof:', ecdh instanceof crypto.ECDH);
console.log('ecdh ctor instanceof:', crypto.ECDH('prime256v1') instanceof crypto.ECDH);
console.log('curves has secp384r1:', crypto.getCurves().indexOf('secp384r1') >= 0);
