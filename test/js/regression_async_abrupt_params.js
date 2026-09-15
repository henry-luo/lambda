// An async function rejects its own promise when parameter instantiation is
// abrupt (a failing destructuring pattern or a throwing default). Two MIR
// lowering defects broke this for arrows and awaiting bodies:
//  - expression-bodied async arrows read an unrelated register at the
//    function-level error exit, so the promise never settled or rejected with
//    a garbage value;
//  - bodies containing `await` routed the parameter throw to an error exit
//    that was never emitted, which crashed MIR linking for the whole script.
// Generators and async generators still throw synchronously at the call.

function boom() { throw new Error("default"); }
let captured = 7;

const cases = [
  ["arrow destructure, expression body", async ({ a }) => a],
  ["arrow destructure, block body", async ({ a }) => { return a; }],
  ["arrow default, expression body", async (x = boom()) => x],
  ["arrow default, block body", async (x = boom()) => { return x; }],
  ["arrow array pattern, expression body", async ([a]) => a],
  ["arrow closure, expression body", async ({ a }) => a + captured],
  ["arrow destructure, awaiting body", async ({ a }) => { await null; return a; }],
  ["arrow default, awaiting body", async (x = boom()) => { await null; return x; }],
  ["function destructure", async function ({ a }) { return a; }],
  ["function default", async function (x = boom()) { return x; }],
  ["function destructure, awaiting body", async function ({ a }) { await null; return a; }],
  ["function with arguments, awaiting body", async function ({ a }) { await null; return arguments.length + a; }],
  ["object method destructure", { async m({ a }) { return a; } }.m],
  ["class method default, awaiting body", new (class { async m(x = boom()) { await null; return x; } })().m],
];

(async function () {
  for (const [label, fn] of cases) {
    let promise;
    try {
      promise = fn(undefined);
    } catch (e) {
      console.log(label + ": sync throw " + e.constructor.name);
      continue;
    }
    try {
      const value = await promise;
      console.log(label + ": resolved " + value);
    } catch (e) {
      console.log(label + ": rejected " + e.constructor.name);
    }
  }
  // A successful call still resolves through the same entries.
  console.log("ok: " + await (async ({ a }) => a)({ a: 1 }) + " " + await (async ({ a }) => { await null; return a; })({ a: 2 }));
  for (const [label, fn] of [
    ["generator destructure", function* ({ a }) { yield a; }],
    ["async generator destructure", async function* ({ a }) { yield a; }],
  ]) {
    try { fn(undefined); console.log(label + ": no throw"); }
    catch (e) { console.log(label + ": sync throw " + e.constructor.name); }
  }
})();
