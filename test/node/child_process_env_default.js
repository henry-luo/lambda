var cp = require('node:child_process');

process.env.LAMBDA_CHILD_PROCESS_ENV_DEFAULT = 'visible';

var child = cp.spawn('/usr/bin/env', [], {});
var output = '';

console.log('stdout setEncoding type:', typeof child.stdout.setEncoding);
child.stdout.setEncoding('utf8');
child.stdout.on('data', function(chunk) {
  output += String(chunk);
});

child.on('close', function(code) {
  console.log('default env inherited:', output.indexOf('LAMBDA_CHILD_PROCESS_ENV_DEFAULT=visible') !== -1);
  console.log('default env close:', code);
});
