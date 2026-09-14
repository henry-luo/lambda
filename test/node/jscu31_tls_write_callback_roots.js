const assert = require('assert');
const fs = require('fs');
const tls = require('tls');

const key = fs.readFileSync('ref/node/test/fixtures/keys/agent1-key.pem');
const cert = fs.readFileSync('ref/node/test/fixtures/keys/agent1-cert.pem');
const callbacks = [];
const server = tls.createServer({ key, cert });

server.listen(0, () => {
  const client = tls.connect(server.address().port, { rejectUnauthorized: false });
  client.write('first', () => { callbacks.push('first'); });
  client.write('second', () => { callbacks.push('second'); });
  setTimeout(() => {
    assert.deepStrictEqual(callbacks, ['first', 'second']);
    client.destroy();
    server.close();
    console.log('tls write callback roots ok');
  }, 250);
});
