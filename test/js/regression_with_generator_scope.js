// JS05-L1 (JSCU44): a `with` scope belongs to the activation that opened it.
// Before the per-activation chain, the scope chain was one process-wide stack,
// so a generator suspended inside `with` left its scope object visible to every
// other activation — including unrelated top-level code.

// 1. A suspended generator's scope must not be visible outside it.
function* suspendsInsideWith() {
  const scope = { leaked_binding: 1 };
  with (scope) { yield 1; yield 2; }
}
const it = suspendsInsideWith();
it.next();                                   // suspended INSIDE with (scope)
console.log(typeof leaked_binding === "undefined");
it.next();
console.log(typeof leaked_binding === "undefined");

// 2. The generator keeps its own scope across the suspension.
function* readsAcrossYield() {
  const scope = { carried: 1 };
  with (scope) {
    yield carried;                           // 1
    carried = 5;
    yield carried;                           // 5
  }
}
const carrier = readsAcrossYield();
console.log(carrier.next().value === 1);
console.log(carrier.next().value === 5);

// 3. A `with` scope must not leak into a callee, on either call lane.
function calleeProbe() { return typeof callee_visible; }
function callerWithScope() {
  const scope = { callee_visible: 9 };
  with (scope) { return calleeProbe(); }
}
console.log(callerWithScope() === "undefined");

// 4. A closure created inside `with` keeps the scope after the block exits.
function captureFromWith() {
  const scope = { captured: 7 };
  with (scope) { return function () { return captured; }; }
}
console.log(captureFromWith()() === 7);

// 5. Nested generators must not observe each other's scopes.
function* outerGen() {
  const outerScope = { only_outer: 1 };
  with (outerScope) { yield 1; }
}
function* innerGen() {
  yield typeof only_outer;
}
const outer = outerGen();
outer.next();                                // suspended inside with (outerScope)
console.log(innerGen().next().value === "undefined");
outer.next();
