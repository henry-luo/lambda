var cp = require('node:child_process');
var argv = Array.isArray(process.argv) ? process.argv : [];
var script = typeof __filename === 'string' ? __filename : 'test/node/child_process_ipc_messaging.js';
var execPath = typeof process.execPath === 'string' ? process.execPath : './lambda.exe';

if (argv[2] === 'child') {
  console.log('child connected start:', process.connected);
  process.on('message', function(msg) {
    console.log('child message:', msg.kind + ':' + msg.value);
    process.send({ kind: 'pong', value: msg.value + 1 });
    process.disconnect();
    console.log('child connected after disconnect:', process.connected);
  });
} else {
  var child = cp.spawn(execPath, ['js', script, 'child', '--no-log'], {
    stdio: ['ignore', 'pipe', 'inherit', 'ipc']
  });
  var out = '';
  child.stdout.on('data', function(chunk) {
    out += String(chunk);
  });
  child.on('message', function(msg) {
    console.log('parent message:', msg.kind + ':' + msg.value);
  });
  child.on('disconnect', function() {
    console.log('parent connected on disconnect:', child.connected);
  });
  child.on('close', function(code) {
    out.trim().split('\n').forEach(function(line) {
      if (line) console.log(line);
    });
    console.log('parent close:', code);
  });
  console.log('parent connected start:', child.connected);
  console.log('parent send return:', child.send({ kind: 'ping', value: 41 }));
}
