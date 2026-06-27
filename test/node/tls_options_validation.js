const assert = require('assert');
const tls = require('tls');

function expectInvalid(fn, prop, suffix) {
  assert.throws(fn, {
    code: 'ERR_INVALID_ARG_TYPE',
    name: 'TypeError',
    message: `The "options.${prop}" property must be of type string or an instance of Buffer, TypedArray, or DataView.${suffix}`,
  });
}

expectInvalid(() => tls.createServer({ key: true, cert: false }), 'key',
  ' Received type boolean (true)');
expectInvalid(() => tls.createServer({ key: false, cert: 1 }), 'cert',
  ' Received type number (1)');
expectInvalid(() => tls.createServer({ key: false, cert: false, ca: {} }), 'ca',
  ' Received an instance of Object');
expectInvalid(() => tls.createServer({ key: [Buffer.from('k'), true], cert: false }), 'key',
  ' Received type boolean (true)');

assert.doesNotThrow(() => tls.createSecureContext({ key: 0, cert: 0, ca: 0 }));

console.log('tls options validation ok');
