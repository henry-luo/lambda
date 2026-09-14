const assert = require('assert');
const http = require('http');

const order = [];
const server = http.createServer((req, res) => {
  res.end('ok', () => { order.push('end'); });
});

server.listen(0, () => {
  http.get({ host: '127.0.0.1', port: server.address().port }, (res) => {
    let body = '';
    res.on('data', (chunk) => { body += String(chunk); });
    res.on('end', () => {
      order.push('response');
      assert.strictEqual(body, 'ok');
      assert.deepStrictEqual(order, ['end', 'response']);
      server.close(() => { console.log('http response write tail ok'); });
    });
  });
});
