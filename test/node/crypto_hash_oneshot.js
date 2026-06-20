const crypto = require('crypto');

const input = Buffer.from('Lambda hash');
const hex = crypto.hash('sha256', input);
const expectedHex = crypto.createHash('sha256').update(input).digest('hex');
console.log('hex equals:', hex === expectedHex);

const base64 = crypto.hash('sha384', 'abc', 'base64');
const expectedBase64 = crypto.createHash('sha384').update('abc').digest('base64');
console.log('base64 equals:', base64 === expectedBase64);

const digest = crypto.hash('sha512', 'abc', 'buffer');
const expectedDigest = crypto.createHash('sha512').update('abc').digest('buffer');
console.log('buffer length:', digest.length);
console.log('buffer equals:', Buffer.from(digest).toString('hex') === Buffer.from(expectedDigest).toString('hex'));

try {
  crypto.hash(null, 'x');
  console.log('bad alg: false');
} catch (err) {
  console.log('bad alg:', err.code);
}

try {
  crypto.hash('sha256', {});
  console.log('bad data: false');
} catch (err) {
  console.log('bad data:', err.code);
}

try {
  crypto.hash('sha256', 'x', 0);
  console.log('bad enc type: false');
} catch (err) {
  console.log('bad enc type:', err.code);
}

try {
  crypto.hash('sha256', 'x', 'bad');
  console.log('bad enc value: false');
} catch (err) {
  console.log('bad enc value:', err.code);
}
