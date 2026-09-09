const childProcess = require('node:child_process');
const argv = Array.isArray(process.argv) ? process.argv : [];
const script = typeof __filename === 'string'
  ? __filename
  : 'test/js/jscu31_child_process_ipc_callback_slots.js';
const execPath = typeof process.execPath === 'string' ? process.execPath : './lambda.exe';

if (argv[2] === 'child') {
  process.on('message', (message) => {
    process.send({ value: message.value + 1 });
    process.disconnect();
  });
} else {
  const child = childProcess.spawn(execPath, ['js', script, 'child', '--no-log'], {
    stdio: ['ignore', 'ignore', 'inherit', 'ipc'],
  });
  let callbackOk = false;
  let reply = 0;
  child.on('message', (message) => { reply = message.value; });
  child.on('close', (code) => {
    console.log('child process callback:', callbackOk);
    console.log('child process reply:', reply);
    console.log('child process close:', code);
  });
  child.send({ value: 41 }, (error) => { callbackOk = error === undefined; });
}
