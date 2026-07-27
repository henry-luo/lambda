const constants = require('node:constants');

console.log(constants === require('constants'));
console.log(constants.F_OK === 0);
console.log(typeof constants.EACCES);
console.log(typeof constants.PRIORITY_NORMAL);
console.log(typeof constants.SIGTERM);
console.log(Object.getPrototypeOf(constants) === null);
console.log(Object.isFrozen(constants));
