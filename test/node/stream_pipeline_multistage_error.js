const assert = require('assert');
const { Readable, Transform, Writable, pipeline } = require('stream');

{
  const read = new Readable({ read() {} });
  const transform = new Transform({
    transform(chunk, enc, cb) {
      cb(new Error('kaboom'));
    }
  });
  const write = new Writable({
    write(chunk, enc, cb) {
      cb();
    }
  });

  let readClosed = false;
  let transformClosed = false;
  let writeClosed = false;
  let readErrors = 0;
  let transformErrors = 0;
  let writeErrors = 0;
  read.on('close', () => { readClosed = true; });
  transform.on('close', () => { transformClosed = true; });
  write.on('close', () => { writeClosed = true; });

  read.on('error', (err) => {
    readErrors++;
    assert.strictEqual(err.message, 'kaboom');
  });
  transform.on('error', (err) => {
    transformErrors++;
    assert.strictEqual(err.message, 'kaboom');
  });
  write.on('error', (err) => {
    writeErrors++;
    assert.strictEqual(err.message, 'kaboom');
  });

  pipeline(read, transform, write, (err) => {
    assert.strictEqual(err.message, 'kaboom');
    setTimeout(() => {
      assert.strictEqual(readClosed, true);
      assert.strictEqual(transformClosed, true);
      assert.strictEqual(writeClosed, true);
      assert.strictEqual(readErrors, 1);
      assert.strictEqual(transformErrors, 1);
      assert.strictEqual(writeErrors, 1);
      runArrayForm();
    }, 10);
  });

  read.push('hello');
}

function runArrayForm() {
  const read = new Readable({ read() {} });
  let result = '';
  const write = new Writable({
    write(chunk, enc, cb) {
      result += chunk;
      cb();
    }
  });

  pipeline([read, write], (err) => {
    assert.strictEqual(err, undefined);
    assert.strictEqual(result, 'helloworld');
    console.log('stream pipeline multistage error ok');
  });

  read.push('hello');
  read.push('world');
  read.push(null);
}
