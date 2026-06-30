'use strict';

const assert = require('assert');
const crypto = require('crypto');
const fs = require('fs');

const privateKey = [
  '-----BEGIN PRIVATE KEY-----',
  'MIICdQIBADANBgkqhkiG9w0BAQEFAASCAl8wggJbAgEAAoGBALMRa/LQPt9zxZhl',
  'cjEZau9aoHi+0wdx7e81vcMFFS2Z3hc3qCRT8l0QDyxeXEQXgCiiu/9sIhYEevAQ',
  'PmUvhVA/weoGO268KEwXdqL0xBfJLtSLt/yvTSwMk+/JNgO2RZcu8zM2IgxBpIaU',
  '58vnacdWafkinGJpLtM71MV8M6JdAgMBAAECgYB9/4o3hnRXAr1MqEUba0klNl2n',
  '2I3gtTe4k9X8fX0TYys0pwL23OKyvPQQQi0l9GtHLIqgBVROrcRbWvKsfC2Oxauv',
  '4444ZomjdM8JGl9sgQJFjtKNUu9PWjNJN8JLojHG7gnhCuzHOmYLEX4bY7baC5H6',
  'U8XwFrNwZvjpBxhUgQJBAOfMwckzVOeMa4L/Btda72+iHtf0GKdMSqJGK68mNtvg',
  'Q3p/ZRM54wXKMe/OA8h9lG8ChVCPeiVsIugxXrkDZm0CQQDFw1GRUwu8l9Mhv8d/',
  'TWJkudnha3deri80fAB2ayLdXRI+PC9ITXDoX8HjnV5d0cDGUTmWSwSNJ0Zsvuzw',
  '8nWxAkAI9Jw4Dcel+oLc2MWG5HiDs5vFdCTPsd7gTh258pwD+rIgtXNOPtpKivlK',
  '7oau5Esrzskfx6tMbtUaa23hcAQxAkAWmHp5YEO3CKHW+VKR+QWE/LcoSl8ZMk2y',
  'cXicDyGsqTWsZrQATtjXtBkzKIffsFeWUTGDOo8KkbI6OpZX8VwBAkAJ84Q7YsbY',
  '3/vZc9VgcZ1EIMtLvEf3kHgW5/xngocRtTadbrg63lxiOOcFv84IcirrCCDX7kOc',
  'x0K8TQoqaRAc',
  '-----END PRIVATE KEY-----'
].join('\n') + '\n';

const publicKey = [
  '-----BEGIN PUBLIC KEY-----',
  'MIGfMA0GCSqGSIb3DQEBAQUAA4GNADCBiQKBgQCzEWvy0D7fc8WYZXIxGWrvWqB4',
  'vtMHce3vNb3DBRUtmd4XN6gkU/JdEA8sXlxEF4Aoorv/bCIWBHrwED5lL4VQP8Hq',
  'BjtuvChMF3ai9MQXyS7Ui7f8r00sDJPvyTYDtkWXLvMzNiIMQaSGlOfL52nHVmn5',
  'IpxiaS7TO9TFfDOiXQIDAQAB',
  '-----END PUBLIC KEY-----'
].join('\n') + '\n';

const message = 'Node4 asymmetric verify slice';
const signer = crypto.createSign('RSA-SHA256');
assert.strictEqual(signer.update(message.slice(0, 7)), signer);
assert.strictEqual(signer.update(message.slice(7)), signer);
const signature = signer.sign({ key: privateKey });
console.log('rsa sign options object:', signature.length > 0);

const verifier = crypto.createVerify('sha256');
verifier.update(message);
assert.strictEqual(verifier.verify(publicKey, signature), true);
console.log('rsa verify public pem:', true);

const tampered = crypto.createVerify('sha256');
tampered.update(message + '!');
assert.strictEqual(tampered.verify(publicKey, signature), false);
console.log('rsa verify tampered:', false);

const base64Signature = crypto.createSign('sha256')
  .update(message)
  .sign(privateKey, 'base64');
const base64Verifier = crypto.createVerify('RSA-SHA256');
base64Verifier.update(message);
assert.strictEqual(base64Verifier.verify({ key: publicKey }, base64Signature, 'base64'), true);
console.log('rsa verify options base64:', true);

const pssOptions = {
  padding: crypto.constants.RSA_PKCS1_PSS_PADDING,
  saltLength: 32
};
const pssSignature = crypto.createSign('sha256')
  .update(message)
  .sign({ key: privateKey, padding: pssOptions.padding, saltLength: pssOptions.saltLength });
assert.strictEqual(crypto.createVerify('sha256')
  .update(message)
  .verify({ key: publicKey, padding: pssOptions.padding, saltLength: pssOptions.saltLength }, pssSignature), true);
assert.strictEqual(crypto.createVerify('sha256')
  .update(message)
  .verify({ key: publicKey, padding: pssOptions.padding, saltLength: 20 }, pssSignature), false);
console.log('rsa pss sign verify options:', true);

const privateKeyObject = crypto.createPrivateKey({ key: Buffer.from(privateKey) });
const publicKeyObject = crypto.createPublicKey({ key: Buffer.from(publicKey) });
assert.strictEqual(privateKeyObject.type, 'private');
assert.strictEqual(publicKeyObject.type, 'public');
assert.strictEqual(privateKeyObject.asymmetricKeyType, 'rsa');
assert.strictEqual(publicKeyObject.asymmetricKeyType, 'rsa');
console.log('rsa keyobject properties:', true);

assert.strictEqual(privateKeyObject.asymmetricKeyDetails.modulusLength, 1024);
assert.strictEqual(publicKeyObject.asymmetricKeyDetails.modulusLength, 1024);
assert.strictEqual(privateKeyObject.asymmetricKeyDetails.publicExponent.toString(), '65537');
assert.strictEqual(publicKeyObject.asymmetricKeyDetails.publicExponent.toString(), '65537');
assert.strictEqual(typeof privateKeyObject.asymmetricKeyDetails.publicExponent, 'bigint');
console.log('rsa keyobject details:', true);

const objectSignature = crypto.createSign('sha256')
  .update(message)
  .sign(privateKeyObject);
assert.strictEqual(crypto.createVerify('sha256')
  .update(message)
  .verify(publicKeyObject, objectSignature), true);
console.log('rsa keyobject sign verify:', true);

const privateDer = privateKeyObject.export({ type: 'pkcs8', format: 'der' });
assert.strictEqual(crypto.createVerify('sha256')
  .update(message)
  .verify(privateKey, objectSignature), true);
assert.strictEqual(crypto.createVerify('sha256')
  .update(message)
  .verify(privateKeyObject, objectSignature), true);
assert.strictEqual(crypto.verify('sha256', Buffer.from(message), {
  key: privateDer,
  format: 'der',
  type: 'pkcs8'
}, objectSignature), true);
console.log('rsa verify private key material:', true);

const oneShotSignature = crypto.sign('sha256', Buffer.from(message), {
  key: privateKeyObject,
  padding: crypto.constants.RSA_PKCS1_PSS_PADDING
});
const oneShotMaxSaltLength = privateKeyObject.asymmetricKeyDetails.modulusLength / 8 -
  crypto.hash('sha256', Buffer.from(message), 'buffer').byteLength - 2;
assert.strictEqual(crypto.verify('sha256', Buffer.from(message), {
  key: publicKeyObject,
  padding: crypto.constants.RSA_PKCS1_PSS_PADDING,
  saltLength: oneShotMaxSaltLength
}, oneShotSignature), true);
assert.strictEqual(crypto.verify('sha256', Buffer.from(message + '!'), {
  key: publicKeyObject,
  padding: crypto.constants.RSA_PKCS1_PSS_PADDING,
  saltLength: oneShotMaxSaltLength
}, oneShotSignature), false);
console.log('rsa one-shot pss default salt:', true);

const derivedPublic = crypto.createPublicKey(privateKeyObject);
assert.strictEqual(derivedPublic.type, 'public');
assert.strictEqual(crypto.createVerify('sha256')
  .update(message)
  .verify(derivedPublic, objectSignature), true);
assert.strictEqual(publicKeyObject.equals(derivedPublic), true);
assert.strictEqual(publicKeyObject.equals(privateKeyObject), false);
console.log('rsa keyobject derive public:', true);
console.log('rsa keyobject equals:', true);

assert.strictEqual(publicKeyObject.export().byteLength > 0, true);
assert.strictEqual(publicKeyObject.export({ type: 'spki', format: 'pem' }).includes('BEGIN PUBLIC KEY'), true);
assert.strictEqual(privateKeyObject.export({ type: 'pkcs8', format: 'pem' }).includes('BEGIN PRIVATE KEY'), true);
assert.strictEqual(Buffer.isBuffer(publicKeyObject.export({ type: 'spki', format: 'der' })), true);
assert.strictEqual(Buffer.isBuffer(privateKeyObject.export({ type: 'pkcs8', format: 'der' })), true);
assert.throws(() => publicKeyObject.export({ type: 'pkcs8', format: 'pem' }), {
  code: 'ERR_INVALID_ARG_VALUE'
});
console.log('rsa keyobject export options:', true);

const privatePkcs1Pem = privateKeyObject.export({ type: 'pkcs1', format: 'pem' });
const publicPkcs1Pem = publicKeyObject.export({ type: 'pkcs1', format: 'pem' });
const privatePkcs1Der = privateKeyObject.export({ type: 'pkcs1', format: 'der' });
const publicPkcs1Der = publicKeyObject.export({ type: 'pkcs1', format: 'der' });
assert.strictEqual(privatePkcs1Pem.includes('BEGIN RSA PRIVATE KEY'), true);
assert.strictEqual(publicPkcs1Pem.includes('BEGIN RSA PUBLIC KEY'), true);
assert.strictEqual(Buffer.isBuffer(privatePkcs1Der), true);
assert.strictEqual(Buffer.isBuffer(publicPkcs1Der), true);
assert.strictEqual(privatePkcs1Der[0], 0x30);
assert.strictEqual(publicPkcs1Der[0], 0x30);
const pkcs1Signature = crypto.sign('sha256', Buffer.from(message),
  crypto.createPrivateKey(privatePkcs1Pem));
assert.strictEqual(crypto.verify('sha256', Buffer.from(message),
  crypto.createPublicKey(publicPkcs1Pem), pkcs1Signature), true);
assert.strictEqual(crypto.verify('sha256', Buffer.from(message), {
  key: publicPkcs1Der,
  format: 'der',
  type: 'pkcs1'
}, pkcs1Signature), true);
console.log('rsa keyobject pkcs1 export:', true);
assert.strictEqual(privateKeyObject instanceof crypto.KeyObject, true);
assert.strictEqual(publicKeyObject instanceof crypto.KeyObject, true);
console.log('rsa keyobject export instanceof:', true);

const generated = crypto.generateKeyPairSync('rsa', {
  modulusLength: 1024,
  publicExponent: 0x10001
});
assert.strictEqual(generated.privateKey.type, 'private');
assert.strictEqual(generated.publicKey.type, 'public');
assert.strictEqual(generated.privateKey.asymmetricKeyType, 'rsa');
assert.strictEqual(generated.publicKey.asymmetricKeyType, 'rsa');
assert.strictEqual(generated.privateKey.asymmetricKeyDetails.modulusLength, 1024);
assert.strictEqual(generated.publicKey.asymmetricKeyDetails.publicExponent.toString(), '65537');
assert.strictEqual(generated.privateKey instanceof crypto.KeyObject, true);
assert.strictEqual(generated.publicKey instanceof crypto.KeyObject, true);
console.log('rsa generateKeyPairSync keyobjects:', true);

const generatedSignature = crypto.createSign('sha256')
  .update(message)
  .sign({ key: generated.privateKey, padding: crypto.constants.RSA_PKCS1_PSS_PADDING, saltLength: 20 });
assert.strictEqual(crypto.createVerify('sha256')
  .update(message)
  .verify({ key: generated.publicKey, padding: crypto.constants.RSA_PKCS1_PSS_PADDING, saltLength: 20 }, generatedSignature), true);
console.log('rsa generateKeyPairSync pss:', true);

const generatedSmall = crypto.generateKeyPairSync('rsa', {
  modulusLength: 512
});
assert.strictEqual(generatedSmall.publicKey.type, 'public');
assert.strictEqual(generatedSmall.privateKey.type, 'private');
assert.strictEqual(generatedSmall.publicKey.asymmetricKeyType, 'rsa');
assert.strictEqual(generatedSmall.publicKey.asymmetricKeyDetails.modulusLength, 512);
assert.strictEqual(generatedSmall.privateKey.asymmetricKeyDetails.publicExponent.toString(), '65537');
console.log('rsa generateKeyPairSync 512 keyobjects:', true);

const ecPrivateKey = [
  '-----BEGIN PRIVATE KEY-----',
  'MIGHAgEAMBMGByqGSM49AgEGCCqGSM49AwEHBG0wawIBAQQgDxBsPQPIgMuMyQbx',
  'zbb9toew6Ev6e9O6ZhpxLNgmAEqhRANCAARfSYxhH+6V5lIg+M3O0iQBLf+53kuE',
  '2luIgWnp81/Ya1Gybj8tl4tJVu1GEwcTyt8hoA7vRACmCHnI5B1+bNpS',
  '-----END PRIVATE KEY-----'
].join('\n') + '\n';

const ecPublicKey = [
  '-----BEGIN PUBLIC KEY-----',
  'MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEX0mMYR/uleZSIPjNztIkAS3/ud5L',
  'hNpbiIFp6fNf2GtRsm4/LZeLSVbtRhMHE8rfIaAO70QApgh5yOQdfmzaUg==',
  '-----END PUBLIC KEY-----'
].join('\n') + '\n';

const ecPrivateKeyObject = crypto.createPrivateKey(ecPrivateKey);
const ecPublicKeyObject = crypto.createPublicKey(ecPublicKey);
assert.strictEqual(ecPrivateKeyObject.asymmetricKeyType, 'ec');
assert.strictEqual(ecPublicKeyObject.asymmetricKeyType, 'ec');
const ecP1363Signature = crypto.sign('sha256', Buffer.from(message), {
  key: ecPrivateKeyObject,
  dsaEncoding: 'ieee-p1363'
});
assert.strictEqual(ecP1363Signature.length, 64);
assert.strictEqual(crypto.verify('sha256', Buffer.from(message), {
  key: ecPublicKeyObject,
  dsaEncoding: 'ieee-p1363'
}, ecP1363Signature), true);
assert.strictEqual(crypto.verify('sha256', Buffer.from(message), {
  key: ecPublicKeyObject,
  dsaEncoding: 'der'
}, ecP1363Signature), false);
console.log('ec keyobject p1363 sign verify:', true);

const ed25519PrivateKey = [
  '-----BEGIN PRIVATE KEY-----',
  'MC4CAQAwBQYDK2VwBCIEIMFSujN0jIUIdzSvuxka0lfgVVkMdRTuaVvIYUHrvzXQ',
  '-----END PRIVATE KEY-----'
].join('\n') + '\n';
const ed25519PublicKey = [
  '-----BEGIN PUBLIC KEY-----',
  'MCowBQYDK2VwAyEAK1wIouqnuiA04b3WrMa+xKIKIpfHetNZRv3h9fBf768=',
  '-----END PUBLIC KEY-----'
].join('\n') + '\n';
const ed448PrivateKey = [
  '-----BEGIN PRIVATE KEY-----',
  'MEcCAQAwBQYDK2VxBDsEOdOtCnu9bDdBqSHNNZ5xoDA5KdLBTUNPcKFaOADNX32s',
  'dfpo52pCtPqfku/l3/OfUHsF43EfZsaaWA==',
  '-----END PRIVATE KEY-----'
].join('\n') + '\n';
const ed448PublicKey = [
  '-----BEGIN PUBLIC KEY-----',
  'MEMwBQYDK2VxAzoAoX/ee5+jlcU53+BbGRsGIzly0V+SZtJ/oGXY0udf84q2hTW2',
  'RdstLktvwpkVJOoNb7oDgc2V5ZUA',
  '-----END PUBLIC KEY-----'
].join('\n') + '\n';
const dsaPrivateKey = fs.readFileSync('ref/node/test/fixtures/keys/dsa_private.pem', 'utf8');
const dsaPublicKey = fs.readFileSync('ref/node/test/fixtures/keys/dsa_public.pem', 'utf8');
const invalidDsaPrivateKey = [
  '-----BEGIN DSA PRIVATE KEY-----',
  'AA==',
  '-----END DSA PRIVATE KEY-----'
].join('\n') + '\n';
const ed25519PrivateKeyObject = crypto.createPrivateKey(ed25519PrivateKey);
const ed25519PublicKeyObject = crypto.createPublicKey(ed25519PublicKey);
assert.strictEqual(ed25519PrivateKeyObject.asymmetricKeyType, 'ed25519');
assert.strictEqual(ed25519PublicKeyObject.asymmetricKeyType, 'ed25519');
const ed25519Signature = crypto.sign(undefined, Buffer.from(message), ed25519PrivateKeyObject);
assert.strictEqual(ed25519Signature.length, 64);
assert.strictEqual(crypto.verify(undefined, Buffer.from(message), ed25519PublicKeyObject,
  ed25519Signature), true);
assert.strictEqual(crypto.verify(undefined, Buffer.from(message + '!'), ed25519PublicKey,
  ed25519Signature), false);
assert.strictEqual(crypto.verify(undefined, Buffer.from(message), {
  key: ed25519PublicKeyObject.export({ type: 'spki', format: 'der' }),
  format: 'der',
  type: 'spki'
}, ed25519Signature), true);
console.log('ed25519 keyobject sign verify:', true);

const ed448PrivateKeyObject = crypto.createPrivateKey(ed448PrivateKey);
const ed448PublicKeyObject = crypto.createPublicKey(ed448PublicKey);
assert.strictEqual(ed448PrivateKeyObject.asymmetricKeyType, 'ed448');
assert.strictEqual(ed448PublicKeyObject.asymmetricKeyType, 'ed448');
const ed448Signature = crypto.sign(undefined, Buffer.from(message), {
  key: ed448PrivateKeyObject.export({ type: 'pkcs8', format: 'der' }),
  format: 'der',
  type: 'pkcs8'
});
assert.strictEqual(ed448Signature.length, 114);
assert.strictEqual(crypto.verify(undefined, Buffer.from(message), ed448PublicKeyObject,
  ed448Signature), true);
assert.strictEqual(crypto.verify(undefined, Buffer.from(message), Buffer.from(ed448PublicKey),
  ed448Signature), true);
console.log('ed448 keyobject sign verify:', true);

const dsaPrivateKeyObject = crypto.createPrivateKey(dsaPrivateKey);
const dsaPublicKeyObject = crypto.createPublicKey(dsaPublicKey);
assert.strictEqual(dsaPrivateKeyObject.asymmetricKeyType, 'dsa');
assert.strictEqual(dsaPublicKeyObject.asymmetricKeyType, 'dsa');
assert.strictEqual(dsaPrivateKeyObject.asymmetricKeyDetails.modulusLength, 2048);
assert.strictEqual(dsaPrivateKeyObject.asymmetricKeyDetails.divisorLength, 256);
const dsaDerSignature = crypto.sign('sha256', Buffer.from(message), dsaPrivateKeyObject);
assert.strictEqual(crypto.verify('sha256', Buffer.from(message), dsaPublicKeyObject,
  dsaDerSignature), true);
assert.strictEqual(crypto.verify('sha256', Buffer.from(message + '!'), dsaPublicKeyObject,
  dsaDerSignature), false);
const dsaP1363Signature = crypto.sign('sha256', Buffer.from(message), {
  key: dsaPrivateKeyObject,
  dsaEncoding: 'ieee-p1363'
});
assert.strictEqual(dsaP1363Signature.length,
  Math.ceil(dsaPrivateKeyObject.asymmetricKeyDetails.divisorLength / 8) * 2);
assert.strictEqual(crypto.verify('sha256', Buffer.from(message), {
  key: dsaPublicKeyObject.export({ type: 'spki', format: 'der' }),
  format: 'der',
  type: 'spki',
  dsaEncoding: 'ieee-p1363'
}, dsaP1363Signature), true);
const dsaDerivedPublic = crypto.createPublicKey(dsaPrivateKeyObject);
assert.strictEqual(dsaDerivedPublic.asymmetricKeyType, 'dsa');
assert.strictEqual(crypto.verify('sha256', Buffer.from(message), dsaDerivedPublic,
  dsaDerSignature), true);
console.log('dsa keyobject sign verify:', true);

assert.throws(() => crypto.createPrivateKey(invalidDsaPrivateKey), {
  code: 'ERR_OSSL_UNSUPPORTED',
  message: /dsa/
});
console.log('unsupported asymmetric import errors:', true);

let asyncSignAfterCall = false;
const asyncSignReturn = crypto.sign('sha256', Buffer.from(message), privateKeyObject,
  function(err, asyncSignature) {
    console.log('rsa one-shot sign callback:',
      err === null && Buffer.isBuffer(asyncSignature) && asyncSignAfterCall);
  });
assert.strictEqual(asyncSignReturn, undefined);
asyncSignAfterCall = true;

const asyncVerifySignature = crypto.sign('sha256', Buffer.from(message), privateKeyObject);
let asyncVerifyAfterCall = false;
const asyncVerifyReturn = crypto.verify('sha256', Buffer.from(message), publicKeyObject,
  asyncVerifySignature, function(err, ok) {
    console.log('rsa one-shot verify callback:', err === null && ok === true && asyncVerifyAfterCall);
  });
assert.strictEqual(asyncVerifyReturn, undefined);
asyncVerifyAfterCall = true;

let asyncEcSignAfterCall = false;
const asyncEcSignReturn = crypto.sign('sha256', Buffer.from(message), {
  key: ecPrivateKeyObject,
  dsaEncoding: 'ieee-p1363'
}, function(err, asyncSignature) {
  console.log('ec one-shot p1363 callback:',
    err === null && Buffer.isBuffer(asyncSignature) &&
    asyncSignature.length === 64 && asyncEcSignAfterCall);
});
assert.strictEqual(asyncEcSignReturn, undefined);
asyncEcSignAfterCall = true;
