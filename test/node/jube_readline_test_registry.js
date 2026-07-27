const readline = require('readline');
const readlinePromises = require('readline/promises');
const test = require('node:test');

console.log(readline === require('node:readline'), readline.default === readline);
console.log(typeof readline.createInterface, typeof readline.Interface);
console.log(readlinePromises === require('node:readline/promises'), readlinePromises.default === readlinePromises);
console.log(typeof readlinePromises.createInterface, typeof readlinePromises.Interface);
console.log(test === test.default, test === test.test, typeof test.describe, typeof test.beforeEach);
