const vm = require('vm');
const nodeVm = require('node:vm');

console.log(vm === nodeVm, vm.default === vm);
console.log(typeof vm.createContext, typeof vm.runInNewContext, typeof vm.Script);
console.log(vm.runInNewContext('20 + 22'));
console.log(typeof vm.constants, vm.constants.USE_MAIN_CONTEXT_DEFAULT_LOADER);
