var assert = require('node:assert');
var cp = require('node:child_process');

if (process.argv[2] === 'grandchild') {
  setInterval(function() {}, 1000);
} else if (process.argv[2] === 'child') {
  var grandchild = cp.spawn(process.execPath, [__filename, 'grandchild'], {
    detached: true,
    stdio: 'ignore'
  });
  console.log(grandchild.pid);
  grandchild.unref();
} else {
  var child = cp.spawn(process.execPath, [__filename, 'child']);
  var grandchildPid = -1;

  child.stdout.on('data', function(data) {
    grandchildPid = parseInt(String(data), 10);
  });

  child.on('close', function(code) {
    assert.strictEqual(code, 0);
    assert.notStrictEqual(grandchildPid, -1);
    assert.throws(function() {
      process.kill(child.pid, 0);
    }, /^Error: kill ESRCH$/);
    process.kill(grandchildPid, 'SIGTERM');
    console.log('detached kill ok');
  });
}
