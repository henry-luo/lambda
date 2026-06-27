const assert = require('assert');
const { Duplex } = require('stream');

const halfOpen = new Duplex({
  read() {}
});
assert.strictEqual(halfOpen.allowHalfOpen, true);
halfOpen.on('finish', () => assert.fail('default duplex should remain half-open'));
halfOpen.resume();
halfOpen.push(null);

let sawAutoFinish = false;
const autoEnd = new Duplex({
  allowHalfOpen: false,
  read() {}
});
assert.strictEqual(autoEnd.allowHalfOpen, false);
autoEnd.on('finish', () => {
  sawAutoFinish = true;
});
autoEnd.resume();
autoEnd.push(null);

let sawPreEndedFinish = false;
const preEnded = new Duplex({
  allowHalfOpen: false,
  read() {}
});
preEnded._writableState.ended = true;
preEnded.on('finish', () => {
  sawPreEndedFinish = true;
});
preEnded.resume();
preEnded.push(null);

process.on('exit', () => {
  assert.strictEqual(sawAutoFinish, true);
  assert.strictEqual(sawPreEndedFinish, false);
  console.log('stream duplex half open ok');
});
