var workerThreads = require('node:worker_threads');
var receiveMessageOnPort = workerThreads.receiveMessageOnPort;
var channel = new workerThreads.MessageChannel();
var closedMoveChannel = new workerThreads.MessageChannel();
var receiveChannel = new workerThreads.MessageChannel();
var eventChannel = new workerThreads.MessageChannel();
var eventDrainChannel = new workerThreads.MessageChannel();

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

var eventListenerSeen = 0;
var eventEmitterSeen = 0;
eventChannel.port2.addEventListener('message', function(evt) {
  eventListenerSeen++;
  console.log('event listener:', evt && evt.type, evt && evt.data && evt.data.kind);
});
eventChannel.port2.on('message', function(value) {
  eventEmitterSeen++;
  console.log('event emitter raw:', value && value.kind, value && value.data);
});
eventChannel.port1.postMessage({ kind: 'event' });

var eventDrainSeen = 0;
eventDrainChannel.port2.addEventListener('message', function(evt) {
  eventDrainSeen++;
  console.log('event drain listener:', evt && evt.data);
});
eventDrainChannel.port1.postMessage('drained-event');
var eventDrainedMessage = receiveMessageOnPort(eventDrainChannel.port2);
console.log('receive event drained:', eventDrainedMessage && eventDrainedMessage.message);

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
  console.log('event listener seen:', eventListenerSeen);
  console.log('event emitter seen:', eventEmitterSeen);
  console.log('event drain seen:', eventDrainSeen);

  var closeSeen = 0;
  var closeEventSeen = 0;
  channel.port2.once('close', function() {
    closeSeen++;
    console.log('close once:', closeSeen);
  });
  channel.port2.addEventListener('close', function(evt) {
    closeEventSeen++;
    console.log('close event:', evt && evt.type);
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
    console.log('close event final:', closeEventSeen);
    closedMoveChannel.port2.close();
    try {
      workerThreads.moveMessagePortToContext(closedMoveChannel.port2, {});
    } catch (err) {
      console.log('closed move code:', err && err.code);
    }
    console.log('done');
  }, 10);
}, 10);
