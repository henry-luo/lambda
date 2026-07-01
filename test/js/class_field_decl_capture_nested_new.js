"use strict";
// Bug 4 regression: a class field-initializer arrow that captures an outer
// function declaration must resolve that function even when the class is
// inline-`new`'d inside a nested function. The IIFE-scope function declaration
// is promoted to a module-var slot; the hoist must write the closure through to
// that slot so the nested capture (which reads via js_get_module_var) sees the
// function rather than an undefined slot ("X is not a function").
(() => {
  function helper() { return 42; }

  // 1. top-level inline-new (was already OK)
  class A { m = () => helper(); }
  console.log("top-level: " + new A().m());

  // 2. inline-new inside a nested function (the bug)
  class B { m = () => helper(); }
  function mountB() { return new B(); }
  console.log("nested: " + mountB().m());

  // 3. field arrow that captures the outer fn AND this, dispatched later
  function toIntent(kind) { return kind === "x" ? { ok: true } : null; }
  class C {
    tag = "C";
    run = (kind) => {
      const it = toIntent(kind);
      return it === null ? "null" : this.tag + ":ok";
    };
  }
  function mountC() { return new C(); }
  const c = mountC();
  console.log("capture+this: " + c.run("x") + " / " + c.run("y"));

  // 4. two sibling field arrows, only one captures the outer fn
  class D {
    v = 7;
    a = () => this.v;
    b = () => helper() + this.v;
  }
  function mountD() { return new D(); }
  const d = mountD();
  console.log("siblings: " + d.a() + " " + d.b());
})();
