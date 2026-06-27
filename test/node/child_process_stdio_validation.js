var cp = require('node:child_process');

function thrownCode(fn) {
  try {
    fn();
    return 'none';
  } catch (err) {
    return err.name + ':' + err.code;
  }
}

console.log('bad scalar stdio:', thrownCode(function() {
  cp.spawn('echo', ['ok'], { stdio: 'bogus' });
}));

console.log('ipc scalar stdio:', thrownCode(function() {
  cp.spawn('echo', ['ok'], { stdio: 'ipc' });
}));

console.log('duplicate ipc stdio:', thrownCode(function() {
  cp.spawn('echo', ['ok'], {
    stdio: ['ignore', 'ignore', 'ignore', 'ipc', 'ipc']
  });
}));

var child = cp.spawn('echo', ['ok'], { stdio: 'overlapped' });
console.log('overlapped stdio pid type:', typeof child.pid);
