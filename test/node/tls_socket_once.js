// C0.1 regression: tls once() used to be a straight alias for on(), so the
// listener stayed registered and ran on every emit.
const assert = require('assert');
const fs = require('fs');
const tls = require('tls');

// server side: drive once() through the public emit() surface
const bare = tls.createServer();
let serverOnce = 0;
bare.once('ping', () => { serverOnce++; });
assert.strictEqual(bare.emit('ping', null), true);
assert.strictEqual(bare.emit('ping', null), false);
assert.strictEqual(serverOnce, 1);

// socket side: two 'data' deliveries over a loopback pair; the on() listener
// sees both, the once() listener only the first, and ordering follows
// registration order within the first delivery.
const key = fs.readFileSync('ref/node/test/fixtures/keys/agent1-key.pem');
const cert = fs.readFileSync('ref/node/test/fixtures/keys/agent1-cert.pem');
const order = [];

const server = tls.createServer({ key, cert }, (sock) => {
  sock.write('a');
  setTimeout(() => { sock.write('b'); }, 60);
});

server.listen(0, () => {
  const client = tls.connect(server.address().port, { rejectUnauthorized: false });
  client.on('data', () => { order.push('on'); });
  client.once('data', () => { order.push('once'); });
  setTimeout(() => {
    client.destroy();
    server.close();
    assert.deepStrictEqual(order, ['on', 'once', 'on']);
    console.log('tls socket once ok');
  }, 250);
});
