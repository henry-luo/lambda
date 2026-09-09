// JSCU31: patched Socket.connect sees the complete createConnection argument list.
const net = require("net");
net.Socket.prototype.connect = function(...args) {
    console.log(args.length, args[16]);
    return this;
};
const values = [];
for (let i = 0; i < 17; i++) {
    values.push(i);
}
const socket = net.createConnection(...values);
socket.destroy();
