// return out of `with` now unwinds in the lowering. The finally cases are the
// ones that must NOT close too early: a scope enclosing the try is still live
// while the finally body runs.
const o = { qq: 1 }, p = { rr: 2 };

function plainReturn()      { with (o) { return qq; } }
function nestedReturn()     { with (o) { with (p) { return qq + rr; } } }
function returnsWithValue()  { with (o) { return typeof qq; } }

// the finally body resolves through the scope that encloses the try
let seen = "";
function returnThroughFinally() {
  with (o) {
    try { return qq; } finally { seen = typeof qq; }
  }
}
// a scope opened *inside* the try is closed by the return, before the finally
let innerSeen = "";
function returnFromInnerWith() {
  with (o) {
    try { with (p) { return rr; } } finally { innerSeen = typeof rr; }
  }
}
// nested finallys both run, outer scope still live in each
let order = [];
function doubleFinally() {
  with (o) {
    try { try { return qq; } finally { order.push(typeof qq); } }
    finally { order.push(typeof qq); }
  }
}

console.log(plainReturn() === 1);
console.log(nestedReturn() === 3);
console.log(returnsWithValue() === "number");
console.log(returnThroughFinally() === 1 && seen === "number");
console.log(returnFromInnerWith() === 2 && innerSeen === "undefined");
console.log(doubleFinally() === 1 && order.join(",") === "number,number");
console.log(typeof qq === "undefined" && typeof rr === "undefined");
