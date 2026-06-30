const assert = require('assert');
const fs = require('fs');
const tls = require('tls');

const extraPath = process.env.NODE_EXTRA_CA_CERTS;
const extra = tls.getCACertificates('extra');
if (extraPath) {
  assert.deepStrictEqual(extra, [fs.readFileSync(extraPath, 'utf8')]);
} else {
  assert.deepStrictEqual(extra, []);
}

assert.strictEqual(tls.getCACertificates(), tls.getCACertificates('default'));
assert.strictEqual(tls.getCACertificates('bundled'), tls.rootCertificates);
assert.strictEqual(tls.getCACertificates('bundled'), tls.getCACertificates('bundled'));
assert(Array.isArray(tls.rootCertificates));
assert(tls.rootCertificates.length > 0);

assert.throws(() => tls.getCACertificates(1), { code: 'ERR_INVALID_ARG_TYPE' });
assert.throws(() => tls.getCACertificates('bad'), { code: 'ERR_INVALID_ARG_VALUE' });

const cert = fs.readFileSync('ref/node/test/fixtures/keys/fake-startcom-root-cert.pem', 'utf8');
tls.setDefaultCACertificates([cert, cert]);
assert.deepStrictEqual(tls.getCACertificates('default'), [cert]);
tls.setDefaultCACertificates([]);
assert.deepStrictEqual(tls.getCACertificates(), []);

console.log('tls ca certificates ok');
