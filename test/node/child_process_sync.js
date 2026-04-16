// child_process basic tests — execSync, spawnSync
var cp = require('node:child_process');

// execSync - simple command
var result = cp.execSync('echo hello');
console.log('execSync:', result.trim());

// execSync - with pipe
var result2 = cp.execSync('echo "abc def" | wc -w');
console.log('execSync pipe:', result2.trim());

// spawnSync - simple
var sp = cp.spawnSync('echo', ['spawn', 'test']);
console.log('spawnSync stdout:', sp.stdout.trim());
console.log('spawnSync status:', sp.status);

// spawnSync - ls
var sp2 = cp.spawnSync('ls', ['-1', 'test/node']);
console.log('spawnSync ls has files:', sp2.stdout.length > 0);

// execFileSync
var ef = cp.execFileSync('echo hello');
console.log('execFileSync:', ef.trim());

// spawnSync with nonexistent or failing command
var sp3 = cp.spawnSync('false', []);
console.log('spawnSync false status:', sp3.status);
