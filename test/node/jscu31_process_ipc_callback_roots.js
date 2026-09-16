const childProcess = require('node:child_process');
const argv = Array.isArray(process.argv) ? process.argv : [];
const script = typeof __filename === 'string'
  ? __filename
  : 'test/js/jscu31_process_ipc_callback_roots.js';
const execPath = typeof process.execPath === 'string' ? process.execPath : './lambda.exe';

if (argv[2] === 'child') {
  process.on('message', (message) => {
    process.send({ value: message.value + 1 }, (error) => {
      console.log('child callback:', error === undefined);
      process.disconnect();
    });
  });
} else {
  const child = childProcess.spawn(execPath, ['js', script, 'child', '--no-log'], {
    stdio: ['ignore', 'pipe', 'inherit', 'ipc'],
  });
  let output = '';
  child.stdout.on('data', (chunk) => { output += String(chunk); });
  child.on('message', (message) => { console.log('parent reply:', message.value); });
  child.on('close', (code) => {
    output.trim().split('\n').forEach((line) => { if (line) console.log(line); });
    console.log('parent close:', code);
  });
  child.send({ value: 41 });
}
