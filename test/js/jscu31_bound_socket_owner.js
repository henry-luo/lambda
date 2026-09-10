const assert = require('assert');
const net = require('net');

const bound = net.BoundSocket({ host: '127.0.0.1', port: 0 });
const address = bound.address();
assert.strictEqual(address.address, '127.0.0.1');
assert.strictEqual(typeof bound.fd(), 'number');
gc();
bound.close();
assert.strictEqual(bound.fd(), -1);
console.log('bound socket owner ok');
