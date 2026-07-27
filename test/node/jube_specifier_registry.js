var bare = require('hostobjDemo');
var alias = require('hostobj_demo');
var prefixed = require('node:hostobjDemo');
var moduleApi = require('module');
console.log('path global initially:', typeof globalThis.path);
var path = require('path');
var pathAlias = require('path.js');
var pathPosix = require('node:path/posix');
var pathWin32 = require('node:path/win32');

console.log('namespace function:', typeof bare.create);
console.log('aliases share namespace:', bare === alias && alias === prefixed);
console.log('is builtin:', moduleApi.isBuiltin('hostobjDemo'));
console.log('catalog listed:', moduleApi.builtinModules.indexOf('hostobjDemo') >= 0);
console.log('path registry builtin:', moduleApi.isBuiltin('path') &&
    moduleApi.builtinModules.indexOf('path') >= 0);
console.log('node core path:', path.join('a', 'b'));
console.log('path aliases share namespace:', path === pathAlias && path === pathPosix);
console.log('path win32 distinct:', path !== pathWin32);
console.log('string decoder registry builtin:', moduleApi.isBuiltin('string_decoder') &&
    moduleApi.builtinModules.indexOf('string_decoder') >= 0);
var os = require('node:os');
console.log('os registry builtin:', moduleApi.isBuiltin('os') &&
    moduleApi.builtinModules.indexOf('os') >= 0);
console.log('os namespace:', os.platform() + ':' + os.constants.signals.SIGINT);
