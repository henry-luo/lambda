// Strict code may not assign `eval` or `arguments` in any target position.
// The early-error pass enforces it, which also covers strict direct eval code
// on both caller tiers.
function check(label, source) {
  try {
    (0, eval)('"use strict"; ' + source);
    console.log(label, "accepted");
  } catch (e) {
    console.log(label, e.name);
  }
}

check("simple:", "function f() { arguments = 1; }");
check("compound:", "function f() { arguments += 1; }");
check("logical:", "function f() { eval ||= 1; }");
check("update:", "function f() { eval++; }");
check("array-pattern:", "function f() { [arguments] = [1]; }");
check("object-pattern:", "function f() { ({ a: eval } = {}); }");
check("shorthand-pattern:", "function f() { ({ arguments } = {}); }");
check("default-pattern:", "function f() { [eval = 1] = []; }");
check("for-of-head:", "for (arguments of []) ;");
check("for-in-pattern:", "for ([eval] in {}) ;");
check("member-targets:", "function f() { var o = {}; o.arguments = 1; o[arguments] = 2; ({ arguments: o.x } = {}); for (o.eval in {}) ; }");

// strictness inherited by direct eval from a strict caller
function strictCaller(source) {
  "use strict";
  try { eval(source); return "accepted"; } catch (e) { return e.name; }
}
console.log("direct-compound:", strictCaller("arguments += 1"));
console.log("direct-pattern:", strictCaller("[arguments] = [1]"));
console.log("direct-member:", strictCaller("var o = {}; o.arguments = 1"));
