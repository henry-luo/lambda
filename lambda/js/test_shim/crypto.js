'use strict';

// Lambda-compatible subset of Node's common/crypto test helper.

let common;
try {
  common = require('./index');
} catch (err) {
  common = null;
}
if (common === null || common === undefined) {
  common = require('./common_index');
}

if (!common.hasCrypto) {
  common.skip('missing crypto');
}

const assert = require('assert');
const crypto = require('crypto');

const modp2buf = Buffer.from([
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xc9, 0x0f,
  0xda, 0xa2, 0x21, 0x68, 0xc2, 0x34, 0xc4, 0xc6, 0x62, 0x8b,
  0x80, 0xdc, 0x1c, 0xd1, 0x29, 0x02, 0x4e, 0x08, 0x8a, 0x67,
  0xcc, 0x74, 0x02, 0x0b, 0xbe, 0xa6, 0x3b, 0x13, 0x9b, 0x22,
  0x51, 0x4a, 0x08, 0x79, 0x8e, 0x34, 0x04, 0xdd, 0xef, 0x95,
  0x19, 0xb3, 0xcd, 0x3a, 0x43, 0x1b, 0x30, 0x2b, 0x0a, 0x6d,
  0xf2, 0x5f, 0x14, 0x37, 0x4f, 0xe1, 0x35, 0x6d, 0x6d, 0x51,
  0xc2, 0x45, 0xe4, 0x85, 0xb5, 0x76, 0x62, 0x5e, 0x7e, 0xc6,
  0xf4, 0x4c, 0x42, 0xe9, 0xa6, 0x37, 0xed, 0x6b, 0x0b, 0xff,
  0x5c, 0xb6, 0xf4, 0x06, 0xb7, 0xed, 0xee, 0x38, 0x6b, 0xfb,
  0x5a, 0x89, 0x9f, 0xa5, 0xae, 0x9f, 0x24, 0x11, 0x7c, 0x4b,
  0x1f, 0xe6, 0x49, 0x28, 0x66, 0x51, 0xec, 0xe6, 0x53, 0x81,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
]);

function assertApproximateSize(key, expectedSize) {
  const unit = typeof key === 'string' ? 'chars' : 'bytes';
  const min = Math.floor(0.9 * expectedSize);
  const max = Math.ceil(1.1 * expectedSize);
  assert(key.length >= min,
         'Key (' + key.length + ' ' + unit + ') is shorter than expected (' +
         min + ' ' + unit + ')');
  assert(key.length <= max,
         'Key (' + key.length + ' ' + unit + ') is longer than expected (' +
         max + ' ' + unit + ')');
}

function testEncryptDecrypt(publicKey, privateKey) {
  const message = 'Hello Node.js world!';
  const plaintext = Buffer.from(message, 'utf8');
  for (const key of [publicKey, privateKey]) {
    const ciphertext = crypto.publicEncrypt(key, plaintext);
    const received = crypto.privateDecrypt(privateKey, ciphertext);
    assert.strictEqual(received.toString('utf8'), message);
  }
}

function testSignVerify(publicKey, privateKey) {
  const message = Buffer.from('Hello Node.js world!');
  const signature = crypto.sign('SHA256', message, privateKey);
  for (const key of [publicKey, privateKey]) {
    assert(crypto.verify('SHA256', message, key, signature));
  }

  const streamSignature = crypto.createSign('SHA256').update(message).sign(privateKey);
  for (const key of [publicKey, privateKey]) {
    assert(crypto.createVerify('SHA256').update(message).verify(key, streamSignature));
  }
}

function getRegExpForPEM(label, cipher) {
  const head = '\\-\\-\\-\\-\\-BEGIN ' + label + '\\-\\-\\-\\-\\-';
  const rfc1421Header = cipher == null ? '' :
    '\nProc-Type: 4,ENCRYPTED\nDEK-Info: ' + cipher + ',[^\\n]+\\n';
  const body = '([a-zA-Z0-9\\+/=]{64}\\n)*[a-zA-Z0-9\\+/=]{1,64}';
  const end = '\\-\\-\\-\\-\\-END ' + label + '\\-\\-\\-\\-\\-';
  return new RegExp('^' + head + rfc1421Header + '\\n' + body + '\\n' + end + '\\n$');
}

const pkcs1PubExp = getRegExpForPEM('RSA PUBLIC KEY');
const pkcs1PrivExp = getRegExpForPEM('RSA PRIVATE KEY');
const pkcs1EncExp = function(cipher) {
  return getRegExpForPEM('RSA PRIVATE KEY', cipher);
};
const spkiExp = getRegExpForPEM('PUBLIC KEY');
const pkcs8Exp = getRegExpForPEM('PRIVATE KEY');
const pkcs8EncExp = getRegExpForPEM('ENCRYPTED PRIVATE KEY');
const sec1Exp = getRegExpForPEM('EC PRIVATE KEY');
const sec1EncExp = function(cipher) {
  return getRegExpForPEM('EC PRIVATE KEY', cipher);
};

function opensslVersionNumber(major, minor, patch) {
  major = major || 0;
  minor = minor || 0;
  patch = patch || 0;
  assert(major >= 0 && major <= 0xf);
  assert(minor >= 0 && minor <= 0xff);
  assert(patch >= 0 && patch <= 0xff);
  return (major << 28) | (minor << 20) | (patch << 4);
}

let OPENSSL_VERSION_NUMBER;

function hasOpenSSL(major, minor, patch) {
  if (!common.hasCrypto) return false;
  if (!process.versions || !process.versions.openssl) return false;
  if (OPENSSL_VERSION_NUMBER === undefined) {
    const match = /^(\d+)\.(\d+)\.(\d+)/.exec(process.versions.openssl);
    if (!match) return false;
    OPENSSL_VERSION_NUMBER = opensslVersionNumber(Number(match[1]),
                                                  Number(match[2]),
                                                  Number(match[3]));
  }
  return OPENSSL_VERSION_NUMBER >= opensslVersionNumber(major, minor, patch);
}

let opensslCli = null;

function detectOpenSSLCli() {
  if (opensslCli !== null) return opensslCli;

  let spawnSync;
  try {
    spawnSync = require('child_process').spawnSync;
  } catch (err) {
    opensslCli = false;
    return opensslCli;
  }

  const path = require('path');
  const candidates = ['openssl'];
  if (process.execPath) {
    candidates.push(path.join(path.dirname(process.execPath), 'openssl-cli'));
  }

  for (const candidate of candidates) {
    try {
      const result = spawnSync(candidate, ['version']);
      if (result && result.status === 0 && result.error === undefined) {
        opensslCli = candidate;
        return opensslCli;
      }
    } catch (err) {
      // try the next candidate
    }
  }

  opensslCli = false;
  return opensslCli;
}

module.exports = {
  modp2buf,
  assertApproximateSize,
  testEncryptDecrypt,
  testSignVerify,
  pkcs1PubExp,
  pkcs1PrivExp,
  pkcs1EncExp,
  spkiExp,
  pkcs8Exp,
  pkcs8EncExp,
  sec1Exp,
  sec1EncExp,
  hasOpenSSL,
  get hasOpenSSL3() {
    return hasOpenSSL(3);
  },
  get opensslCli() {
    return detectOpenSSLCli();
  },
};
