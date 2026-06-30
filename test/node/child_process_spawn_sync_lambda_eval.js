var cp = require('node:child_process');

var child = cp.spawnSync(process.execPath, [
  '-e',
  'console.log("inline:" + process.argv[1]);',
  'argv-value'
]);

console.log('status:', child.status);
console.log('stdout:', child.stdout.trim());
