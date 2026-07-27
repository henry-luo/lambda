const nodeConsole = require('node:console');
const bareConsole = require('console.js');
const nodeProcess = require('node:process');
const bareProcess = require('process');
const buffer = require('buffer');
const vm = require('vm');

console.log(nodeConsole === console);
console.log(bareConsole === console);
console.log(nodeProcess === process);
console.log(bareProcess === process);
console.log(typeof nodeConsole.log);
console.log(typeof nodeProcess.cwd);
console.log(Buffer === buffer.Buffer);
console.log(vm === globalThis.vm);
