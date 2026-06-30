var workerThreads = require('node:worker_threads');
var receiveMessageOnPort = workerThreads.receiveMessageOnPort;
var channel = new workerThreads.MessageChannel();
var closedMoveChannel = new workerThreads.MessageChannel();
var receiveChannel = new workerThreads.MessageChannel();

console.log('receive empty:', receiveMessageOnPort(receiveChannel.port2));
receiveChannel.port1.postMessage({ name: 'alpha' });
receiveChannel.port1.postMessage('beta');
var receivedFirst = receiveMessageOnPort(receiveChannel.port2);
var receivedSecond = receiveMessageOnPort(receiveChannel.port2);
console.log('receive first:', receivedFirst && receivedFirst.message && receivedFirst.message.name);
console.log('receive second:', receivedSecond && receivedSecond.message);
console.log('receive empty after:', receiveMessageOnPort(receiveChannel.port2));

var receiveListenerSeen = 0;
receiveChannel.port2.on('message', function(value) {
  receiveListenerSeen++;
  console.log('receive listener:', value);
});
receiveChannel.port1.postMessage('drained');
var drainedMessage = receiveMessageOnPort(receiveChannel.port2);
console.log('receive drained:', drainedMessage && drainedMessage.message);

try {
  receiveMessageOnPort({});
} catch (err) {
  console.log('receive invalid code:', err && err.code);
}

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
  console.log('receive listener seen:', receiveListenerSeen);

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
    closedMoveChannel.port2.close();
    try {
      workerThreads.moveMessagePortToContext(closedMoveChannel.port2, {});
    } catch (err) {
      console.log('closed move code:', err && err.code);
    }
    console.log('done');
  }, 10);
}, 10);
