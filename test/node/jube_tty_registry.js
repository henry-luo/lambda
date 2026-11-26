const tty = require('tty');
const nodeTty = require('node:tty');

console.log(tty === nodeTty, tty.default === tty);
console.log(typeof tty.isatty, typeof tty.ReadStream, typeof tty.WriteStream);
console.log(typeof new tty.ReadStream(), typeof new tty.WriteStream());
