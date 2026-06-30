var workerThreads = require('node:worker_threads');
var MessageChannel = workerThreads.MessageChannel;
var markAsUntransferable = workerThreads.markAsUntransferable;
var isMarkedAsUntransferable = workerThreads.isMarkedAsUntransferable;
var receiveMessageOnPort = workerThreads.receiveMessageOnPort;

function thrownNameAndCode(fn) {
  try {
    fn();
    return 'none';
  } catch (err) {
    return String(err && err.name) + ':' + String(err && err.code);
  }
}

var primitiveMarks = [0, null, false, true, undefined].map(function(value) {
  markAsUntransferable(value);
  return isMarkedAsUntransferable(value);
});
console.log('primitive marks:', primitiveMarks.join(','));

var obj = {};
var arr = [];
markAsUntransferable(obj);
markAsUntransferable(arr);
console.log('object mark:', isMarkedAsUntransferable(obj));
console.log('array mark:', isMarkedAsUntransferable(arr));

function Foo() {}
markAsUntransferable(Foo.prototype);
console.log('prototype mark:', isMarkedAsUntransferable(Foo.prototype));
console.log('instance inherited mark:', isMarkedAsUntransferable(new Foo()));

var ab = new ArrayBuffer(8);
markAsUntransferable(ab);
var abChannel = new MessageChannel();
console.log('arraybuffer mark:', isMarkedAsUntransferable(ab));
console.log('arraybuffer transfer:', thrownNameAndCode(function() {
  abChannel.port1.postMessage(ab, [ab]);
}));
console.log('arraybuffer byteLength:', ab.byteLength);

var source = new MessageChannel();
var target = new MessageChannel();
markAsUntransferable(target.port1);
console.log('port mark:', isMarkedAsUntransferable(target.port1));
console.log('port transfer:', thrownNameAndCode(function() {
  source.port1.postMessage(target.port1, [target.port1]);
}));
target.port1.postMessage('still works');
var received = receiveMessageOnPort(target.port2);
console.log('marked port still works:', received && received.message);
