var workerThreads = require('node:worker_threads');
var MessageChannel = workerThreads.MessageChannel;
var receiveMessageOnPort = workerThreads.receiveMessageOnPort;

function thrownNameMessageCode(fn) {
  try {
    fn();
    return 'none';
  } catch (err) {
    return String(err && err.name) + ':' + String(err && err.message) + ':' + String(err && err.code);
  }
}

var base = new MessageChannel();
var movedChannel = new MessageChannel();

base.port1.postMessage({ moved: movedChannel.port1 }, [movedChannel.port1]);
var envelope = receiveMessageOnPort(base.port2);
var moved = envelope && envelope.message && envelope.message.moved;
moved.postMessage('through moved port');
var received = receiveMessageOnPort(movedChannel.port2);
console.log('moved message:', received && received.message);
console.log('original detached send:', thrownNameMessageCode(function() {
  movedChannel.port1.postMessage('after transfer');
}));

var dupChannel = new MessageChannel();
var dupPort = new MessageChannel().port1;
console.log('duplicate port:', thrownNameMessageCode(function() {
  dupChannel.port1.postMessage(dupPort, [dupPort, dupPort]);
}));

var dupBuffer = new ArrayBuffer(4);
console.log('duplicate arraybuffer:', thrownNameMessageCode(function() {
  dupChannel.port1.postMessage(dupBuffer, [dupBuffer, dupBuffer]);
}));
console.log('duplicate arraybuffer byteLength:', dupBuffer.byteLength);
