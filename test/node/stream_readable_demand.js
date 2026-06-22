const assert = require('assert');
const { Readable } = require('stream');

class SequencedReadable extends Readable {
  constructor() {
    super();
    this._chunks = 3;
  }

  _read(n) {
    assert(n > 0);
    switch (this._chunks--) {
      case 3:
        return process.nextTick(() => this.push('first chunk'));
      case 2:
        return this.push('second to last chunk');
      case 1:
        return setTimeout(() => this.push('last chunk'), 1);
      case 0:
        return this.push(null);
      default:
        throw new Error('unexpected read');
    }
  }
}

const readable = new SequencedReadable();
const results = [];
let onceCalls = 0;

readable.once('readable', () => {
  onceCalls++;
});

readable.on('readable', () => {
  let chunk;
  while (null !== (chunk = readable.read())) {
    results.push(String(chunk));
  }
});

class SynchronousReadable extends Readable {
  constructor() {
    super({ highWaterMark: 4 });
    this._items = ['a', 'b'];
  }

  _read() {
    while (this._items.length) {
      this.push(this._items.shift());
    }
    this.push(null);
  }
}

const syncReadable = new SynchronousReadable();
let syncReadableEnd = 0;
let syncReadableData = 0;
syncReadable.once('readable', () => {
  assert(syncReadable.read());
  syncReadable.unshift('z');
  syncReadable.on('data', () => {
    syncReadableData++;
  });
});
syncReadable.on('end', () => {
  syncReadableEnd++;
});

class ExactRefillReadable extends Readable {
  constructor() {
    super({ highWaterMark: 4 });
    this._step = 0;
  }

  _read() {
    switch (this._step++) {
      case 0:
        return this.push('abcd');
      case 1:
        return setTimeout(() => this.push('efgh'), 1);
      default:
        return undefined;
    }
  }
}

const exactReadable = new ExactRefillReadable();
const exactChunks = [];
exactReadable.on('readable', () => {
  let chunk;
  while (null !== (chunk = exactReadable.read(4))) {
    exactChunks.push(String(chunk));
    if (exactChunks.length === 1)
      exactReadable.unshift('zz');
  }
});

process.on('exit', () => {
  assert.strictEqual(onceCalls, 1);
  assert.strictEqual(readable._chunks, -1);
  assert.deepStrictEqual(results, [
    'first chunk',
    'second to last chunk',
    'last chunk'
  ]);
  assert.strictEqual(syncReadableEnd, 1);
  assert(syncReadableData >= 1);
  assert.deepStrictEqual(exactChunks, ['abcd', 'zzef']);
  console.log('stream readable demand ok');
});
