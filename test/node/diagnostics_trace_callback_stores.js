const { AsyncLocalStorage } = require('async_hooks');
const dc = require('diagnostics_channel');

const channel = dc.tracingChannel('lambda-diagnostics-callback');
const store = new AsyncLocalStorage();

channel.start.bindStore(store, () => 'start-store');
channel.asyncStart.bindStore(store, () => 'callback-store');

console.log(store.getStore());

channel.traceCallback((cb, value) => {
  console.log(store.getStore() + ':' + value);
  setImmediate(cb, null, 'done');
}, 0, {}, null, (err, result) => {
  console.log(String(err) + ':' + store.getStore() + ':' + result);
}, 'body');

console.log(store.getStore());
