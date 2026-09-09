const assert = require('assert');
const events = require('events');

const first = new events.EventEmitter();
let count = 0;
first.on('value', () => count++);
gc();
first.emit('value');

const second = require('events');
assert.strictEqual(second, events);
assert.strictEqual(count, 1);

const module_names = [
  'string_decoder', 'constants', 'tty', 'os', 'punycode', 'path',
  'perf_hooks', 'querystring', 'trace_events', 'url', 'worker_threads', 'v8', 'timers',
  'timers/promises',
];
const namespaces = [];
for (const name of module_names) {
  const namespace_item = require(name);
  assert.strictEqual(require(name), namespace_item);
  namespaces.push(namespace_item);
}
gc();
for (let index = 0; index < module_names.length; index++) {
  assert.strictEqual(require(module_names[index]), namespaces[index]);
}

process.setUncaughtExceptionCaptureCallback(() => {});
gc();
assert.strictEqual(process.hasUncaughtExceptionCaptureCallback(), true);
process.setUncaughtExceptionCaptureCallback(null);
assert.strictEqual(process.hasUncaughtExceptionCaptureCallback(), false);
console.log('jube persistent value slots ok');
