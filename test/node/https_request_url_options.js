const assert = require('assert');
const https = require('https');
const url = require('url');

function capture(input, options) {
  let seen;
  const agent = {
    createSocket(req, opts) {
      seen = {
        host: opts.hostname || opts.host,
        port: opts.port,
        path: opts.path,
        header: req._header,
      };
      req.destroy();
    }
  };

  const requestOptions = options || {};
  requestOptions.agent = agent;
  https.get(input, requestOptions);

  return seen;
}

function captureOptions(options) {
  let seen;
  options.agent = {
    createSocket(req, opts) {
      seen = { port: opts.port };
      req.destroy();
    }
  };
  https.get(options);
  return seen;
}

let seen = capture('https://example.com:8443/from-string?x=1');
assert.strictEqual(seen.host, 'example.com');
assert.strictEqual(seen.port, '8443');
assert.strictEqual(seen.path, '/from-string?x=1');
assert.ok(seen.header.startsWith('GET /from-string?x=1 HTTP/1.1'));

seen = capture(url.parse('https://parsed.test:9443/from-parse?q=1'));
assert.strictEqual(seen.host, 'parsed.test');
assert.strictEqual(seen.port, '9443');
assert.ok(seen.header.startsWith('GET /from-parse?q=1 HTTP/1.1'));

seen = capture(new URL('https://url-object.test:10443/from-url?z=1'));
assert.strictEqual(seen.host, 'url-object.test');
assert.strictEqual(seen.port, '10443');
assert.ok(seen.header.startsWith('GET /from-url?z=1 HTTP/1.1'));

seen = capture('https://original.test:11443/from-options', {
  hostname: 'override.test',
  port: '12443',
});
assert.strictEqual(seen.host, 'override.test');
assert.strictEqual(seen.port, '12443');
assert.ok(seen.header.startsWith('GET /from-options HTTP/1.1'));

seen = captureOptions({ hostname: 'default-port.test' });
assert.strictEqual(seen.port, 443);

console.log('https request url options ok');
