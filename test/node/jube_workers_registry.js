const workers = require('worker_threads');
const nodeWorkers = require('node:worker_threads');

console.log(workers === nodeWorkers, workers.default === workers);
console.log(workers.isMainThread, workers.threadId, workers.parentPort === null);
console.log(typeof workers.MessageChannel, typeof workers.MessagePort);
console.log(typeof workers.markAsUntransferable, typeof workers.receiveMessageOnPort);

const transferable = {};
workers.markAsUntransferable(transferable);
console.log(workers.isMarkedAsUntransferable(transferable));
