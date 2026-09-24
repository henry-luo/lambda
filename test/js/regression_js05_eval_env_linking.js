// JS05-L5..L7 (D8.1.3v21): a direct eval from interpreted code links to its
// caller's live environments instead of projecting them through the bridge.
function t(label, fn) {
  try { console.log(label, fn()); } catch (e) { console.log(label, "THROWS", e.name); }
}

// JS05-L5: a closure created by eval keeps the caller's bindings live
t("l5-param-closure:", () => { function f(a) { return eval("() => a"); } return f(7)(); });
t("l5-after-return:", () => {
  function f() { var n = 1; var read = eval("() => n"); n = 2; return read; }
  return f()();
});
t("l5-write-through:", () => {
  function f() { var n = 1; eval("(() => { n = 5; })()"); return n; }
  return f();
});
t("l5-block-let:", () => { function f() { { let b = "block"; return eval("() => b"); } } return f()(); });

// JS05-L6: `arguments` and new.target come from the caller frame
t("l6-arguments:", () => {
  function f(a, b) { return eval("arguments.length + ':' + arguments[1]"); }
  return f(1, 2);
});
t("l6-arrow-arguments:", () => { function f(a) { return (() => eval("arguments[0]"))(); } return f("x"); });
t("l6-new-target:", () => { function F() { this.t = eval("typeof new.target"); } return new F().t; });
t("l6-call-new-target:", () => { function F() { return eval("new.target"); } return F(); });

// JS05-L7: an eval var redeclaring the caller's own var aliases it...
t("l7-redeclare:", () => { function f() { var x = 1; return eval("var x; x"); } return f(); });
t("l7-assign:", () => { function f() { var x = 1; eval("var x = 2"); return x; } return f(); });
// ...but never an outer function's var: the reference resolved before the RHS
// ran keeps it (test262 S11.13.1_A6_T1; V8 prints 1/0 here)
t("l7-outer:", () => {
  var x = 0;
  var inner = (function () { x = (eval("var x;"), 1); return x; })();
  return inner + "/" + x;
});

// resolution order, TDZ, immutable bindings, and `with`
t("tdz:", () => { function f() { return eval("q"); let q = 1; } return f(); });
t("const-sloppy:", () => { function f() { const k = 1; eval("k = 2"); } return f(); });
t("const-strict:", () => { "use strict"; const k = 1; eval("k = 2"); });
t("lexical-conflict:", () => { function f() { { let x; { eval("var x;"); } } } return f(); });
t("catch-var:", () => {
  function f() { try { throw 1; } catch (e) { eval("var e = 5"); return e; } }
  return f();
});
t("with-captured:", () => {
  var o = { a: "with" }, g;
  with (o) { g = function (a) { return eval("a"); }; }
  return g("param");
});
t("delete-eval-var:", () => {
  function f() { eval("var d = 1"); return delete d; }
  return f();
});
t("nested-eval:", () => { function f() { var v = "a"; return eval("var v = 'b'; eval('v')"); } return f(); });

// Annex B: an eval block function skips a caller lexical of its name
t("annexb-skip:", () => {
  function f() { { let g = 1; eval("{ function g() {} }"); return typeof g; } }
  return f();
});
t("annexb-param:", () => {
  var after;
  (function (g) { eval("{ function g() {} } after = g;"); })(1);
  return typeof after;
});

// super and private names resolve through the caller's home object
t("super:", () => {
  class A { m() { return "A"; } }
  class B extends A { m() { return eval("super.m()"); } }
  return new B().m();
});
t("super-call:", () => {
  class A { constructor() { this.a = 1; } }
  class B extends A { constructor() { eval("super()"); this.b = 2; } }
  const b = new B();
  return b.a + b.b;
});
t("private:", () => { class C { #x = 5; m() { return eval("this.#x"); } } return new C().m(); });

// A var that eval declares in a parameter initializer conflicts with
// `arguments`; the async function rejects with that SyntaxError (test262
// eval-code/direct/*-declare-arguments; V8 resolves instead).
(async function (p = eval("var arguments")) {})().then(
  () => console.log("async-param: resolved"),
  (e) => console.log("async-param:", e.name));
