'use strict';

const crypto = require('crypto');

function dataViewSum(view, start, end) {
  let sum = 0;
  for (let i = start; i < end; i++) sum += view.getUint8(i);
  return sum;
}

function dataViewAllZero(view, start, end) {
  return dataViewSum(view, start, end) === 0;
}

const buf = Buffer.alloc(12);
const before = buf.toString('hex');
const returned = crypto.randomFillSync(buf, 4, 4);
const after = buf.toString('hex');
console.log('sync same:', returned === buf);
console.log('sync prefix:', after.slice(0, 8) === before.slice(0, 8));
console.log('sync middle:', after.slice(8, 16) !== before.slice(8, 16));
console.log('sync suffix:', after.slice(16) === before.slice(16));

const words = new Uint16Array(4);
crypto.randomFillSync(words);
let wordSum = 0;
for (let i = 0; i < words.length; i++) wordSum += words[i];
console.log('typed changed:', wordSum > 0);

const viewBuffer = new ArrayBuffer(16);
const view = new DataView(viewBuffer);
crypto.randomFillSync(view, 2, 12);
console.log('dataview prefix:', dataViewAllZero(view, 0, 2));
console.log('dataview middle:', dataViewSum(view, 2, 14) > 0);
console.log('dataview suffix:', dataViewAllZero(view, 14, 16));

const ab = new ArrayBuffer(16);
const abView = new DataView(ab);
crypto.randomFillSync(ab, 2, 12);
console.log('arraybuffer prefix:', dataViewAllZero(abView, 0, 2));
console.log('arraybuffer changed:', dataViewSum(abView, 2, 14) > 0);
console.log('arraybuffer suffix:', dataViewAllZero(abView, 14, 16));

const backing = new ArrayBuffer(20);
const backingBuf = Buffer.from(backing, 10);
const backingBefore = backingBuf.toString('hex');
crypto.randomFillSync(backingBuf);
const backingView = new DataView(backing);
console.log('buffer from ab len:', backingBuf.length);
console.log('buffer from ab changed:', backingBuf.toString('hex') !== backingBefore);
console.log('buffer from ab prefix:', dataViewAllZero(backingView, 0, 10));
console.log('buffer from ab backing:', dataViewSum(backingView, 10, 20) > 0);

let asyncCalled = false;
crypto.randomFill(Buffer.alloc(5), function(err, out) {
  asyncCalled = true;
  console.log('async err:', err === null);
  console.log('async len:', out.length);
});
console.log('async called:', asyncCalled);
console.log('getFips:', crypto.getFips());
