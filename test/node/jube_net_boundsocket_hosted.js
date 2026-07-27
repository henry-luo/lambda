const net = require('net');

const bound = new net.BoundSocket({ host: '127.0.0.1', port: 0 });
const address = bound.address();
console.log('bound', address.family, address.port > 0, typeof bound.fd() === 'number');

const server = net.createServer();
server.listen(bound, () => {
  let adopted = '';
  try {
    bound.address();
  } catch (error) {
    adopted = error.code;
  }
  console.log('adopted', adopted, server.address().port === address.port);
  server.close(() => console.log('closed'));
});
