var cp = require('node:child_process');

var child = cp.spawn('cat');
var output = '';

console.log('stdin write type:', typeof child.stdin.write);
console.log('stdin writable:', child.stdin.writable);
console.log('stdin readable:', child.stdin.readable);

child.stdout.setEncoding('utf8');
child.stdout.on('data', function(chunk) {
  output += chunk;
});

child.stdout.on('end', function() {
  console.log('stdout end:', output);
});

child.on('close', function(code) {
  console.log('child close:', code);
});

child.stdin.write('hello');
child.stdin.write(' ');
child.stdin.write('world');
child.stdin.end();
