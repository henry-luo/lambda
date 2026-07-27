const net = require('net');

const server = net.createServer();
console.log(process._getActiveHandles().length);
console.log(process.getActiveResourcesInfo()[0]);

server.close(() => {
  console.log(process._getActiveHandles().length);
  console.log(process.getActiveResourcesInfo().length);
});
