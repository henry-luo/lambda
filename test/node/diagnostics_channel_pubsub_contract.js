const assert = require('assert');
const dc = require('diagnostics_channel');

const { Channel } = dc;
const stringChannel = dc.channel('lambda-diagnostics-pubsub');
const symbolName = Symbol('lambda-diagnostics-symbol');
const symbolChannel = dc.channel(symbolName);

assert.ok(stringChannel instanceof Channel);
assert.ok(symbolChannel instanceof Channel);
assert.strictEqual(dc.channel('lambda-diagnostics-pubsub'), stringChannel);
assert.strictEqual(dc.channel(symbolName), symbolChannel);

let calls = 0;
const subscriber = (message, name) => {
  calls++;
  assert.strictEqual(name, stringChannel.name);
  assert.deepStrictEqual(message, { ok: true });
};

assert.strictEqual(stringChannel.hasSubscribers, false);
stringChannel.subscribe(subscriber);
assert.strictEqual(stringChannel.hasSubscribers, true);
stringChannel.publish({ ok: true });
assert.strictEqual(calls, 1);
assert.strictEqual(stringChannel.unsubscribe(subscriber), true);
assert.strictEqual(stringChannel.unsubscribe(subscriber), false);
assert.strictEqual(stringChannel.hasSubscribers, false);

assert.throws(() => {
  stringChannel.subscribe(null);
}, { code: 'ERR_INVALID_ARG_TYPE' });

assert.throws(() => {
  dc.subscribe('lambda-diagnostics-pubsub', null);
}, { code: 'ERR_INVALID_ARG_TYPE' });

console.log('diagnostics channel pubsub contract ok');
