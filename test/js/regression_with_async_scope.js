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
let observed = "not-run";
async function observer() {
  await null;
  observed = typeof ww;
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

// 4. An async function suspended inside `with` that never resumes must not
//    strand its frame or its scope slot.
async function abandoned() {
  const o = { never: 1 };
  with (o) {
    await new Promise(() => {});
    return typeof never;
  }
}

const a = survivesAwait(), b = observer();
const c = shortLived(), d = longLived();
abandoned();
console.log(typeof ww === "undefined");        // sync observer sees nothing
a.then(v => console.log(v === "number:42"));
b.then(() => console.log(observed === "undefined"));
c.then(v => console.log(v === "number"));
d.then(v => console.log(v === "number,undefined"));
