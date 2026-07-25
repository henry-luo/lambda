'use strict';

const vm = require('vm');
const code = "typeof later === 'undefined'";
const later = vm.runInThisContext(code);

console.log(later);
