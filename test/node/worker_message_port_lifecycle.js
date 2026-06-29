var workerThreads = require('node:worker_threads');
var channel = new workerThreads.MessageChannel();

var removedSeen = 0;
function removedListener(value) {
  removedSeen++;
  console.log('removed listener:', value);
}

channel.port2.on('message', removedListener);
channel.port2.once('message', function(value) {
  console.log('once message:', value);
});
channel.port2.removeListener('message', removedListener);

channel.port1.postMessage('first');
channel.port1.postMessage('second');

setTimeout(function() {
  console.log('removed seen:', removedSeen);

  var closeSeen = 0;
  channel.port2.once('close', function() {
    closeSeen++;
    console.log('close once:', closeSeen);
  });

  channel.port2.on('message', function(value) {
    console.log('late message:', value);
  });
  channel.port1.postMessage('queued-before-close');
  channel.port2.close();
  channel.port1.postMessage('after-close');
  channel.port2.close();

  setTimeout(function() {
    console.log('close final:', closeSeen);
    console.log('done');
  }, 10);
}, 10);
