const os = require('os');

console.log(typeof os.platform() === 'string');
console.log(Array.isArray(os.cpus()));
console.log(Array.isArray(os.loadavg()));
console.log(typeof os.networkInterfaces() === 'object');
console.log(typeof os.userInfo().username === 'string');
console.log(Object.isFrozen(os.constants));
console.log(os === require('node:os'));
