// BlockList owns a dynamic rule collection; the 129th address must remain
// observable instead of being silently discarded by the former fixed table.
const net = require('net');
const list = new net.BlockList();
for (let i = 0; i < 129; i++) {
    list.addAddress('10.0.0.' + i);
}
console.log(list.check('10.0.0.128'));
