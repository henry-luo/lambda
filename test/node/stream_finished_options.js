const assert = require('assert');
const { Duplex, finished } = require('stream');

function makeDuplex() {
  return new Duplex({
    read() {},
    write(chunk, encoding, callback) {
      callback();
    }
  });
}

let finishedCalls = 0;

function expectReadableIgnored(value) {
  const duplex = makeDuplex();
  finished(duplex, { readable: value }, (err) => {
    assert.ifError(err);
    finishedCalls++;
  });
  duplex.end('done');
}

expectReadableIgnored(false);
expectReadableIgnored(0);
expectReadableIgnored('');

assert.throws(() => {
  finished(makeDuplex(), 1, () => {});
}, { code: 'ERR_INVALID_ARG_TYPE' });

process.on('exit', () => {
  assert.strictEqual(finishedCalls, 3);
  console.log('stream finished options ok');
});
