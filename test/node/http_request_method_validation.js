const assert = require('assert');
const http = require('http');

for (const method of [-1, 0, 1, {}, true, false, [], Symbol()]) {
  assert.throws(() => http.request({ method, path: '/' }), {
    code: 'ERR_INVALID_ARG_TYPE',
    name: 'TypeError',
  });
}

console.log('http request method validation ok');
