// A `throw` that leaves its function closes the `with` scopes it opened, the
// same way `return` does. One caught inside the function must not: the
// catch/finally label restores the depth its own try was entered at.
const o = { zz: "outer" }, p = { yy: "inner" };

function caughtInSameFunction() {
  try { with (o) { throw new Error("boom"); } } catch (e) { e; }
  return typeof zz;
}
function finallySeesOutside() {
  let seen = "?";
  try { with (o) { throw new Error("boom"); } }
  catch (e) { e; }
  finally { seen = typeof zz; }
  return seen;
}
// the outer `with` is still live in the catch, the inner one is not
function nestedPartialUnwind() {
  with (o) {
    try { with (p) { throw new Error("boom"); } }
    catch (e) { return typeof zz + "/" + typeof yy; }
  }
}

// a throw with no handler in its own function: the callee's scope must not
// survive into the caller
function throwsOut() { with (o) { throw new Error("boom"); } }
function callerAfterEscape() {
  try { throwsOut(); } catch (e) { e; }
  return typeof zz;
}
function throughIntermediate() { return throwsOut(); }
function outerAfterEscape() {
  try { throughIntermediate(); } catch (e) { e; }
  return typeof zz;
}
// and the binding must not be readable either
function probeAfterThrow() {
  try { throwsOut(); } catch (e) { e; }
  try { return zz; } catch (e) { return "reference-error"; }
}

// each iteration opens a scope; the throw leaves on the second one
function loopThrow() {
  const items = [{ q: 1 }, { q: 2 }];
  try {
    for (const it of items) { with (it) { if (q === 2) throw new Error("boom"); } }
  } catch (e) { e; }
  return typeof q;
}

console.log(caughtInSameFunction() === "undefined");
console.log(finallySeesOutside() === "undefined");
console.log(nestedPartialUnwind() === "string/undefined");
console.log(callerAfterEscape() === "undefined");
console.log(outerAfterEscape() === "undefined");
console.log(probeAfterThrow() === "reference-error");
console.log(loopThrow() === "undefined");
console.log(typeof zz === "undefined" && typeof yy === "undefined");
