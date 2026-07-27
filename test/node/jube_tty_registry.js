const tty = require('tty');
const nodeTty = require('node:tty');
const net = require('node:net');

console.log(tty === nodeTty, tty.default === tty);
console.log(typeof tty.isatty, typeof tty.ReadStream, typeof tty.WriteStream);
console.log(typeof new tty.ReadStream(), typeof new tty.WriteStream());
console.log(Object.getPrototypeOf(tty.ReadStream) === net.Socket,
  Object.getPrototypeOf(tty.ReadStream.prototype) === net.Socket.prototype);
