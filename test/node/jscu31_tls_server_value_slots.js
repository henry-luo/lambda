const assert = require('assert');
const fs = require('fs');
const tls = require('tls');

const key = fs.readFileSync('ref/node/test/fixtures/keys/agent1-key.pem');
const cert = fs.readFileSync('ref/node/test/fixtures/keys/agent1-cert.pem');
let connections = 0;
const server = tls.createServer({ key, cert }, () => {
  connections++;
});

server.listen(0, () => {
  const client = tls.connect(server.address().port, { rejectUnauthorized: false });
  client.on('secureConnect', () => {
    setTimeout(() => {
      client.destroy();
      server.close(() => {
        assert.strictEqual(connections, 1);
        console.log('tls server value slots ok');
      });
    }, 100);
  });
});
