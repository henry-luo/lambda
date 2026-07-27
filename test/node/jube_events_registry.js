const events = require('events');
const emitter = new events.EventEmitter();
let total = 0;

emitter.on('value', (value) => { total += value; });
emitter.once('value', (value) => { total += value * 10; });
emitter.emit('value', 2);
emitter.emit('value', 3);

console.log(total);
console.log(emitter.listenerCount('value'));
console.log(events === require('node:events'));
