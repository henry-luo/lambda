// JSI35 / D8.1.3v20: dynamic source executes in the AST interpreter.
function t(label, fn) {
  try { console.log(label, fn()); } catch (e) { console.log(label, "THROWS", e.name); }
}

// statement completion values (UpdateEmpty)
t("cptn-do-while:", () => eval("1; do { 2; } while (false)"));
t("cptn-decl-after:", () => eval("h(); function h() { return 'H'; }"));
t("cptn-if-empty:", () => eval("1; if (false) 2;"));
t("cptn-finally:", () => eval("try { 3; } finally { 4; }"));
t("cptn-var:", () => eval("5; var cv = 6;"));

// eval-created vars are visible to the caller immediately and survive a throw
t("var-after-throw:", () => {
  function f() { try { eval("var y = 1; throw 0"); } catch (e) {} return typeof y; }
  return f();
});
t("mid-eval-closure:", () => {
  function f() { function g() { return typeof z; } return eval("var z = 1; g()"); }
  return f();
});

// the reference resolved before an eval's `var` keeps its outer binding
t("outer-ref:", () => {
  var x = 0;
  var inner = (function () { x = (eval("var x;"), 1); return x; })();
  return inner + "/" + x;
});

// global eval code: strict vars and lexicals stay private to each eval
t("strict-indirect-var:", () => { (0, eval)("'use strict'; var jsi35_sv = 1;"); return typeof jsi35_sv; });
t("indirect-let:", () => { (0, eval)("let jsi35_q = 1;"); return (0, eval)("let jsi35_q = 2; jsi35_q"); });
t("sloppy-indirect-var:", () => {
  (0, eval)("var jsi35_g = 3;");
  return Object.getOwnPropertyDescriptor(globalThis, "jsi35_g").configurable;
});

// sources that create no script
t("blank:", () => eval("  /* c */ // x"));
t("regexp:", () => eval("/ab+c/i").test("ABBC"));
t("unterminated-comment:", () => eval("/* open"));

// a closure created by eval keeps the caller class's private names
t("private-closure:", () => {
  class C { #x = 5; m() { return eval("() => this.#x"); } }
  return new C().m()();
});

// Function constructors
t("function-ctor:", () => new Function("a", "b", "return a + b")(2, 3));
t("function-source:", () => new Function("a", "return a").toString());
t("generator-ctor:", () => {
  var GF = Object.getPrototypeOf(function* () {}).constructor;
  return [...new GF("yield 1; yield 2")()].join(",");
});
