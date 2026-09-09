const assert = require('assert');
const http = require('http');

const callbacks = [];
const server = http.createServer((req, res) => {
  req.on('data', () => {});
  req.on('end', () => { res.end('ok'); });
});

server.listen(0, () => {
  const req = http.request({
    host: '127.0.0.1',
    port: server.address().port,
    method: 'POST',
  }, (res) => {
    res.on('data', () => {});
    res.on('end', () => {
      assert.deepStrictEqual(callbacks, ['first', 'second']);
      server.close();
      console.log('http client write callback roots ok');
    });
  });

  // POST sends chunked headers when no length is known. Delay until that
  // native connection has been established, then queue two independent writes.
  setTimeout(() => {
    req.write('first', () => { callbacks.push('first'); });
    req.write('second', () => { callbacks.push('second'); });
    req.end();
  }, 25);
});
