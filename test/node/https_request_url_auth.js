const assert = require('assert');
const https = require('https');

function capture(input, options) {
  let header;
  const requestOptions = options || {};
  requestOptions.agent = {
    createSocket(req) {
      header = req._header;
      req.destroy();
    }
  };
  https.get(input, requestOptions);
  return header;
}

let header = capture('https://user:pass%3A@example.com/auth');
assert.ok(header.includes('Authorization: Basic dXNlcjpwYXNzOg==\r\n'));

header = capture('https://user:pass@example.com/auth', {
  headers: {
    Authorization: 'NoAuthForYOU',
  },
});
assert.ok(header.includes('Authorization: NoAuthForYOU\r\n'));
assert.ok(!header.includes('Authorization: Basic '));

console.log('https request url auth ok');
