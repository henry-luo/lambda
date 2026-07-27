const repl = require('repl');

console.log(repl === require('node:repl'));
console.log(typeof repl.start, typeof repl.REPLServer, typeof repl.Recoverable);
console.log(repl.REPL_MODE_SLOPPY, repl.REPL_MODE_STRICT);
