const assert = require('assert');
const { AsyncLocalStorage } = require('async_hooks');
const dc = require('diagnostics_channel');
const { describe, it, test } = require('node:test');

const events = [];
dc.subscribe('tracing:node.test:start', (data) => {
  events.push(`start:${data.name}:${data.type}`);
});
dc.subscribe('tracing:node.test:end', (data) => {
  events.push(`end:${data.name}:${data.type}`);
});

const storage = new AsyncLocalStorage();
dc.channel('tracing:node.test:start').bindStore(storage, (data) => data.name);

test('standalone test', () => {
  assert.strictEqual(storage.getStore(), 'standalone test');
});

describe('outer suite', () => {
  it('inner test', () => {
    assert.strictEqual(storage.getStore(), 'inner test');
  });
});

process.on('exit', () => {
  assert.deepStrictEqual(events, [
    'start:standalone test:test',
    'end:standalone test:test',
    'start:outer suite:suite',
    'start:inner test:test',
    'end:inner test:test',
    'end:outer suite:suite',
  ]);
  console.log('runner diagnostics channel: ok');
});
