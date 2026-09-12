// JSCU44: a `with` scope belongs to the activation that opened it, and an
// activation that suspends keeps its scope across the suspension without
// exposing it to anything else. Generators are covered by
// regression_with_generator_scope.js; these are the async paths, plus the
// overlapping case that pins non-LIFO release of the scope slots.

// 1. The scope survives an await, and is writable after resuming.
async function survivesAwait() {
  const o = { ww: 1 };
  with (o) {
    await null;
    ww = 42;
    return typeof ww + ":" + ww;
  }
}

// 2. An unrelated async function running while (1) is suspended must not see it.
let withAsyncObserved = "not-run";
async function observer() {
  await null;
  withAsyncObserved = typeof ww;
}

// 3. Two activations suspended inside `with` at once, finishing in the order
//    they were entered -- so the first-allocated scope slot is released while a
//    later-allocated one is still live. This is the non-LIFO release; a
//    watermark-LIFO store cannot hold these without spilling both.
async function shortLived() {
  const o = { aa: 1 };
  with (o) {
    await null; await null; await null;
    return typeof aa;
  }
}
async function longLived() {
  const o = { bb: 2 };
  with (o) {
    await null; await null; await null; await null; await null;
    return typeof bb + "," + typeof aa;
  }
}

const withAsyncA = survivesAwait(), withAsyncB = observer();
const withAsyncC = shortLived(), withAsyncD = longLived();
console.log(typeof ww === "undefined");        // sync observer sees nothing
withAsyncA.then(v => console.log(v === "number:42"));
withAsyncB.then(() => console.log(withAsyncObserved === "undefined"));
withAsyncC.then(v => console.log(v === "number"));
withAsyncD.then(v => console.log(v === "number,undefined"));

// 5. An async closure created inside `with` keeps that chain across its own
//    suspension. This is what the frame's chain, captured at creation, carries:
//    an async frame captures it the way js_generator_create does, so a resume
//    never inherits whatever chain the resuming turn happens to have.
function withAsyncMakeAsync() {
  const o = { captured: 7 };
  with (o) {
    return async function () {
      await null;
      return typeof captured + ":" + captured;
    };
  }
}
withAsyncMakeAsync()().then(v => console.log(v === "number:7"));
