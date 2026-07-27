const timers = require('node:timers/promises');
const classic = require('node:timers');

console.log(timers === require('timers/promises'));
console.log(timers === require('timers/promises.js'));
console.log(classic === require('timers'));
console.log(typeof timers.setTimeout);
console.log(typeof timers.setImmediate);
console.log(typeof timers.scheduler.wait);
console.log(require('timers').promises === timers);
console.log(classic.default === classic);
console.log(typeof classic.clearTimeout);
timers.setTimeout(0, 'timer-ok').then((value) => console.log(value));
