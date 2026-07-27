const module = require('module');
const nodeModule = require('node:module');

console.log(module === nodeModule, module.default === module, module.Module === module);
console.log(module.isBuiltin('module'), module.isBuiltin('node:module'), module.isBuiltin('tty'));
console.log(module.builtinModules.includes('module'), module.builtinModules.includes('tty'));
console.log(typeof module.createRequire, typeof module.enableCompileCache, typeof module.constants.compileCacheStatus);
