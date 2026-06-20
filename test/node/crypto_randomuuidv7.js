const crypto = require('crypto');

const uuid = crypto.randomUUIDv7();
const uuid2 = crypto.randomUUIDv7({ disableEntropyCache: true });
const uuidv7Regex = /^[0-9a-f]{8}-[0-9a-f]{4}-7[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/;

console.log('type:', typeof uuid);
console.log('format:', uuidv7Regex.test(uuid));
console.log('option format:', uuidv7Regex.test(uuid2));
console.log('unique:', uuid !== uuid2);
console.log('version:', (Buffer.from(uuid.slice(14, 16), 'hex')[0] & 0xf0) === 0x70);
console.log('variant:', (Buffer.from(uuid.slice(19, 21), 'hex')[0] & 0xc0) === 0x80);

const before = Date.now();
const timed = crypto.randomUUIDv7();
const after = Date.now();
const timestamp = parseInt(timed.replace(/-/g, '').slice(0, 12), 16);
console.log('timestamp:', timestamp >= before && timestamp <= after);

try {
  crypto.randomUUIDv7(1);
  console.log('bad options: false');
} catch (err) {
  console.log('bad options:', err.code);
}

try {
  crypto.randomUUIDv7({ disableEntropyCache: '' });
  console.log('bad cache option: false');
} catch (err) {
  console.log('bad cache option:', err.code);
}
