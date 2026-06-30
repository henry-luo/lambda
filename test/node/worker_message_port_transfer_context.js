var fs = require('node:fs').promises;
var vm = require('node:vm');
var workerThreads = require('node:worker_threads');
var MessageChannel = workerThreads.MessageChannel;
var moveMessagePortToContext = workerThreads.moveMessagePortToContext;

(async function() {
  var fh = await fs.open(__filename);
  var channel = new MessageChannel();
  var moved = moveMessagePortToContext(channel.port2, vm.createContext({}));

  moved.onmessageerror = function(event) {
    console.log('messageerror code:', event && event.data && event.data.code);
  };
  moved.onmessage = function(event) {
    console.log('message data:', event && event.data);
  };
  moved.start();

  channel.port1.postMessage(fh, [fh]);
  console.log('fd after transfer:', fh.fd);
  channel.port1.postMessage('after-transfer');

  setTimeout(function() {
    console.log('done');
  }, 10);
})();
