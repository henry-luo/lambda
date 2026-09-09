const assert = require('assert');
const net = require('net');

const callbacks = [];
const server = net.createServer((socket) => {
  socket.on('data', () => {});
  socket.on('end', () => { socket.end(); });
});

server.listen(0, () => {
  const client = net.createConnection({ port: server.address().port });
  // These all arrive before the TCP connect completion. The socket retains
  // their callbacks while the transport-specific pending-byte queue drains.
  client.write('first', () => { callbacks.push('first'); });
  client.write('second', () => { callbacks.push('second'); });
  client.end(() => { callbacks.push('end'); });
  client.on('close', () => {
    assert.deepStrictEqual(callbacks, ['first', 'second', 'end']);
    server.close(() => { console.log('net socket callback slots ok'); });
  });
});
