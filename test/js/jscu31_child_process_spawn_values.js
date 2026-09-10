const childProcess = require('node:child_process');

const controller = new AbortController();
const child = childProcess.spawn(process.execPath,
  ['js', '-e', 'setTimeout(() => {}, 1000)', '--no-log'], {
    signal: controller.signal,
    stdio: ['ignore', 'ignore', 'inherit'],
  });

child.on('error', (error) => {
  console.log('spawn abort error:', error.name);
});
child.on('exit', (code, signal) => {
  console.log('spawn abort exit:', code, signal);
});

controller.abort(new Error('rooted abort'));
