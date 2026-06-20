'use strict';

const commonCrypto = require('../../lambda/js/test_shim/crypto');

const pem64 = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
const rsaPublic = '-----BEGIN RSA PUBLIC KEY-----\n' +
                  pem64 + '\n' +
                  'AAAA\n' +
                  '-----END RSA PUBLIC KEY-----\n';
const encryptedRsaPrivate = '-----BEGIN RSA PRIVATE KEY-----\n' +
                            'Proc-Type: 4,ENCRYPTED\n' +
                            'DEK-Info: AES-128-CBC,ABCDEF\n' +
                            '\n' +
                            pem64 + '\n' +
                            'BBBB\n' +
                            '-----END RSA PRIVATE KEY-----\n';

commonCrypto.assertApproximateSize(Buffer.alloc(10), 10);
commonCrypto.assertApproximateSize('abcdefghij', 10);

console.log('crypto modp2:', commonCrypto.modp2buf.length,
            commonCrypto.modp2buf[0], commonCrypto.modp2buf[127]);
console.log('crypto pem:', commonCrypto.pkcs1PubExp.test(rsaPublic),
            commonCrypto.pkcs1EncExp('AES-128-CBC').test(encryptedRsaPrivate));
console.log('crypto openssl:', typeof commonCrypto.hasOpenSSL,
            commonCrypto.hasOpenSSL(4), typeof commonCrypto.hasOpenSSL3);

const cli = commonCrypto.opensslCli;
console.log('crypto cli:', cli === false || typeof cli === 'string');
