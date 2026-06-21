'use strict';

const assert = require('assert');
const crypto = require('crypto');

const bytes = crypto.randomBytes(8);
console.log('randomBytes buffer:', Buffer.isBuffer(bytes));
console.log('randomBytes length:', bytes.length);

let bytesCallbackCalled = false;
crypto.randomBytes(5, function(err, out) {
  bytesCallbackCalled = true;
  console.log('randomBytes callback err:', err === null);
  console.log('randomBytes callback buffer:', Buffer.isBuffer(out));
  console.log('randomBytes callback length:', out.length);
});
console.log('randomBytes callback called:', bytesCallbackCalled);

let pseudoWarnings = 0;
process.on('warning', function(warning) {
  if (warning.name === 'DeprecationWarning' && warning.code === 'DEP0115') {
    pseudoWarnings++;
  }
});
crypto.pseudoRandomBytes(1);
crypto.pseudoRandomBytes(1);
console.log('pseudo warning once:', pseudoWarnings === 1);

['pseudoRandomBytes', 'prng', 'rng'].forEach(function(name) {
  const desc = Object.getOwnPropertyDescriptor(crypto, name);
  console.log(name + ' hidden:', !!desc && desc.enumerable === false && desc.configurable === true);
});

const words = new Uint16Array(4);
const returnedWords = globalThis.crypto.getRandomValues(words);
console.log('global crypto object:', typeof globalThis.crypto);
console.log('getRandomValues function:', typeof globalThis.crypto.getRandomValues);
console.log('getRandomValues same:', returnedWords === words);
console.log('getRandomValues length:', returnedWords.length);

const oneArg = crypto.randomInt(3);
const twoArg = crypto.randomInt(1, 3);
console.log('randomInt one arg:', oneArg >= 0 && oneArg < 3);
console.log('randomInt two arg:', twoArg >= 1 && twoArg < 3);

let intCallbackCalled = false;
crypto.randomInt(3, function(err, n) {
  intCallbackCalled = true;
  console.log('randomInt callback err:', err === null);
  console.log('randomInt callback range:', n >= 0 && n < 3);
});
console.log('randomInt callback called:', intCallbackCalled);

assert.throws(function() {
  crypto.randomInt(0);
}, { code: 'ERR_OUT_OF_RANGE', name: 'RangeError' });
console.log('randomInt range error:', true);

assert.throws(function() {
  crypto.randomFillSync(Buffer.alloc(1), 'x');
}, { code: 'ERR_INVALID_ARG_TYPE', name: 'TypeError' });
console.log('randomFill offset type:', true);
