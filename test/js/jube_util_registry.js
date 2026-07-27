const util = require('util');
const nodeUtil = require('node:util');
const types = require('util/types');
const sys = require('sys');
const inherits = require('inherits');

console.log(util === nodeUtil, util.types === types);
console.log(typeof util.format, typeof util.inspect, typeof types.isArray);
console.log(types.isArray([]), types.isDate(new Date(0)));
console.log(sys === util, inherits === util.inherits);
