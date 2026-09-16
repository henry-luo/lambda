const assert = require('assert');
const childProcess = require('node:child_process');

childProcess.exec('printf rooted-exec', (error, stdout, stderr) => {
  assert.strictEqual(error, null);
  assert.strictEqual(stdout, 'rooted-exec');
  assert.strictEqual(stderr, '');
  console.log('child process exec values ok');
});
