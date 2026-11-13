// mbedCrypto-only profile: reference signing key is public RFC 6979 test data.
function equal(actual, expected) {
  if (actual !== expected) throw new Error('unexpected crypto result');
}
function denied(action, code) {
  let caught = false;
  try { action(); } catch (error) { equal(error.code, code); caught = true; }
  equal(caught, true);
}
const crypto = require('crypto');
equal(process.binding('config').hasOpenSSL, false);
equal(process.versions.openssl, undefined);
console.log('provider report:', true);
equal(crypto.createHash('sha256').update('abc').digest('hex'),
  'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad');
equal(crypto.createHmac('sha256', 'key').update('The quick brown fox jumps over the lazy dog').digest('hex'),
  'f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8');
console.log('native digests:', true);
const secret = crypto.createSecretKey(Buffer.from('key'));
equal(secret.symmetricKeySize, 3);
equal(crypto.createHmac('sha256', secret).update('The quick brown fox jumps over the lazy dog').digest('hex'),
  'f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8');
console.log('native key objects:', true);
const privateKey = crypto.createPrivateKey("-----BEGIN PRIVATE KEY-----\nMEECAQAwEwYHKoZIzj0CAQYIKoZIzj0DAQcEJzAlAgEBBCDJr6nYRbp1FmtcIVdnsdaTTlDD2zbomxJ7imIrEg9nIQ==\n-----END PRIVATE KEY-----\n");
const publicKey = crypto.createPublicKey(privateKey);
const signature = crypto.sign('sha256', Buffer.from('sample'), privateKey);
equal(crypto.verify('sha256', Buffer.from('sample'), publicKey, signature), true);
equal(crypto.verify('sha256', Buffer.from('changed'), publicKey, signature), false);
console.log('native P-256:', true);
const rsa = crypto.generateKeyPairSync('rsa', {modulusLength:2048});
const signed = crypto.sign('sha256', Buffer.from('message'), {
  key:rsa.privateKey, padding:crypto.constants.RSA_PKCS1_PSS_PADDING, saltLength:32
});
equal(crypto.verify('sha256', Buffer.from('message'), {
  key:rsa.publicKey, padding:crypto.constants.RSA_PKCS1_PSS_PADDING, saltLength:32
}, signed), true);
equal(crypto.verify('sha256', Buffer.from('message'), {
  key:rsa.publicKey, padding:crypto.constants.RSA_PKCS1_PSS_PADDING, saltLength:0
}, signed), false);
console.log('native RSA-PSS:', true);
denied(() => crypto.generateKeyPairSync('dsa', {modulusLength:2048}),
  'ERR_OSSL_UNSUPPORTED');
console.log('removed DSA:', true);
denied(() => crypto.sign(undefined, Buffer.from('sample'), "-----BEGIN PRIVATE KEY-----\nMC4CAQAwBQYDK2VwBCIEIJ1hsZ3v/VpguoRK9JLsLMREScVpezJpGXA7rAMcrn9g\n-----END PRIVATE KEY-----\n"),
  'ERR_OSSL_CRYPTO_SIGN_FAILED');
console.log('removed EdDSA fallback:', true);
