// JS05-L2: an abrupt jump unwinds only the `with` scopes it actually leaves.
// jm_emit_abrupt_jump_cleanup emitted one js_with_pop per scope open anywhere
// in the function, so a `break` out of a loop *inside* a `with` body closed the
// enclosing scope early and the rest of the body lost its bindings.
const o = { a: 1 }, p = { b: 2 };

// break out of a loop nested inside the with body: the scope stays open
function breakInsideWith() {
  with (o) {
    for (let i = 0; i < 2; i++) { if (i) break; }
    return typeof a;
  }
}
console.log(breakInsideWith() === "number");

// continue is the same jump kind
function continueInsideWith() {
  with (o) {
    for (let i = 0; i < 2; i++) { continue; }
    return typeof a;
  }
}
console.log(continueInsideWith() === "number");

// only the inner of two nested scopes is left
function breakInsideNestedWith() {
  with (o) {
    with (p) { for (;;) { break; } }
    return typeof a + "," + typeof b;
  }
}
console.log(breakInsideNestedWith() === "number,undefined");

// switch break unwinds to the switch, not out of the with
function switchBreakInsideWith() {
  with (o) {
    switch (1) { case 1: break; }
    return typeof a;
  }
}
console.log(switchBreakInsideWith() === "number");

// a labelled break out of the with does close it, and everything inside it
function labelledBreakOutOfWith() {
  outer: with (o) { with (p) { break outer; } }
  return typeof a + "," + typeof b;
}
console.log(labelledBreakOutOfWith() === "undefined,undefined");

// a labelled continue leaving the with closes it for the next iteration
function labelledContinueOutOfWith() {
  L: for (let i = 0; i < 1; i++) { with (o) { continue L; } }
  return typeof a;
}
console.log(labelledContinueOutOfWith() === "undefined");
