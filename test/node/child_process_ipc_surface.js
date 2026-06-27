var cp = require('node:child_process');

function thrownCode(fn) {
  try {
    fn();
    return 'none';
  } catch (err) {
    return err.name + ':' + err.code;
  }
}

console.log('fork bad stdio:', thrownCode(function() {
  cp.fork(__filename, { stdio: '33' });
}));

var child = cp.spawn('__lambda_missing_ipc_child__', [], {
  stdio: ['ignore', 'ignore', 'ignore', 'ipc']
});

console.log('spawn ipc connected:', child.connected);
console.log('spawn ipc send type:', typeof child.send);
console.log('spawn ipc disconnect type:', typeof child.disconnect);
console.log('spawn ipc send return:', child.send({ hello: 'world' }, function(err) {
  console.log('spawn ipc callback err:', err === undefined);
  child.disconnect();
  console.log('spawn ipc disconnected:', child.connected);
  console.log('spawn ipc send after disconnect:', child.send('late'));
}));
