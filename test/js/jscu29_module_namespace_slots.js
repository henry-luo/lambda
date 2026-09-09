// JSCU29: module providers share one exact-root namespace slot store.
const vm = require('vm');
const hooks = require('async_hooks');
const module_api = require('module');
const cluster = require('cluster');
const repl = require('repl');
const domain = require('domain');

gc();
console.log(typeof vm.createContext, typeof hooks.createHook,
    typeof module_api.isBuiltin, typeof cluster.setupPrimary,
    typeof repl.start, typeof domain.create);
