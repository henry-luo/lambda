const EventEmitter = require('events').EventEmitter;
const emitter = new EventEmitter();

emitter.on('value', (value) => console.log(value));
emitter.emit('value', 7);
console.log(emitter._eventsCount);
