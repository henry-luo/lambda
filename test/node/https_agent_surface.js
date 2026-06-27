const assert = require('assert');
const https = require('https');

const agent = https.Agent();

assert.ok(agent instanceof https.Agent);
assert.strictEqual(Object.getPrototypeOf(agent), https.Agent.prototype);
assert.strictEqual(https.Agent.prototype.constructor, https.Agent);
assert.strictEqual(agent.getName(), 'localhost::::::::::::::::::::::');
assert.strictEqual(agent.getName({}), 'localhost::::::::::::::::::::::');

const options = {
  host: '0.0.0.0',
  port: 443,
  localAddress: '192.168.1.1',
  ca: 'ca',
  cert: 'cert',
  clientCertEngine: 'dynamic',
  ciphers: 'ciphers',
  crl: [Buffer.from('c'), Buffer.from('r'), Buffer.from('l')],
  dhparam: 'dhparam',
  ecdhCurve: 'ecdhCurve',
  honorCipherOrder: false,
  key: 'key',
  pfx: 'pfx',
  rejectUnauthorized: false,
  secureOptions: 0,
  secureProtocol: 'secureProtocol',
  servername: 'localhost',
  sessionIdContext: 'sessionIdContext',
  sigalgs: 'sigalgs',
  privateKeyIdentifier: 'privateKeyIdentifier',
  privateKeyEngine: 'privateKeyEngine',
};

assert.strictEqual(
  agent.getName(options),
  '0.0.0.0:443:192.168.1.1:ca:cert:dynamic:ciphers:key:pfx:false:localhost:' +
    '::secureProtocol:c,r,l:false:ecdhCurve:dhparam:0:sessionIdContext:' +
    '"sigalgs":privateKeyIdentifier:privateKeyEngine'
);

console.log('https agent surface ok');
