const assert = require('assert');

let pass = 0;

{
  const stream = new ReadableStream({
    pull(controller) {
      const view = new Uint8Array(new ArrayBuffer(10), 0, 0);
      controller.close();
      assert.throws(() => controller.byobRequest.respondWithNewView(view), {
        name: 'RangeError',
        code: 'ERR_INVALID_ARG_VALUE',
      });
      pass++;
    },
    type: 'bytes',
  });
  const reader = stream.getReader({ mode: 'byob' });
  reader.read(new Uint8Array([1, 2, 3]));
}

{
  let reader;
  const stream = new ReadableStream({
    pull(controller) {
      controller.close();
      const view = new Uint8Array([1, 2, 3]);
      reader.read(view);
      assert.throws(() => controller.byobRequest.respondWithNewView(view), {
        name: 'TypeError',
        code: 'ERR_INVALID_STATE',
      });
      pass++;
    },
    type: 'bytes',
  });
  reader = stream.getReader({ mode: 'byob' });
  reader.read(new Uint8Array([4, 5, 6]));
}

const rejectEmpty = new ReadableStream({
  start(controller) {
    controller.enqueue(new Uint8Array([1, 2, 3]));
  },
  type: 'bytes',
}).getReader({ mode: 'byob' }).read(new Uint8Array());

const rejectEmptyOverBacking = new ReadableStream({
  start(controller) {
    controller.enqueue(new Uint8Array([1, 2, 3]));
  },
  type: 'bytes',
}).getReader({ mode: 'byob' }).read(new Uint8Array(new ArrayBuffer(8), 0, 0));

Promise.all([
  assert.rejects(rejectEmpty, { name: 'TypeError', code: 'ERR_INVALID_STATE' }),
  assert.rejects(rejectEmptyOverBacking, { name: 'TypeError', code: 'ERR_INVALID_STATE' }),
]).then(() => {
  console.log('readablestream byob bad views:', pass);
});
