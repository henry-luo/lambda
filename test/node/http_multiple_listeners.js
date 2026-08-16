// C0.2 regression: http on() used to overwrite the "__on_<event>__" slot, so
// registering a second listener for one event silently dropped the first.
const assert = require('assert');
const http = require('http');

const order = [];

const server = http.createServer();
server.on('request', (req, res) => {
  order.push('request-a');
  res.on('finish', () => { order.push('res-finish-a'); });
  res.on('finish', () => { order.push('res-finish-b'); });
  res.end('ok');
});
server.on('request', () => { order.push('request-b'); });

server.listen(0, () => {
  const port = server.address().port;
  const req = http.request({ port, path: '/' }, (res) => {
    res.resume();
    res.on('end', () => {
      server.close();
      // res.end() emits 'finish' synchronously, so both response listeners run
      // inside the first 'request' listener, before the second one.
      assert.deepStrictEqual(order, [
        'request-a', 'res-finish-a', 'res-finish-b', 'request-b',
        'client-response-a', 'client-response-b',
      ]);
      console.log('http multiple listeners ok');
    });
  });
  req.on('response', () => { order.push('client-response-a'); });
  req.on('response', () => { order.push('client-response-b'); });
  req.end();
});
