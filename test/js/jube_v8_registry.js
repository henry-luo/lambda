const v8 = require('node:v8');

console.log(v8 === require('v8'));
console.log(typeof v8.getHeapStatistics);
console.log(typeof v8.promiseHooks.createHook);
console.log(typeof v8.startupSnapshot.setDeserializeMainFunction);
console.log(v8.default === v8);
