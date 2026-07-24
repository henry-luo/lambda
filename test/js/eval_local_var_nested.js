// Nested direct eval must see vars introduced by a prior direct eval in the
// same function scope (eval-local journal bindings bridged via the env frame).
// Regression: eval("obj.k") after eval("var obj = ...") threw a ReferenceError
// that was silently swallowed (script stopped, exit 0, no message).

function g() {
    console.log("before");
    eval("var obj = {k: 999};");
    console.log("mid", typeof obj);
    console.log(eval("obj.k"));          // expression-form nested eval (member)
    console.log("direct", obj.k);        // read via journal fallback in g
    console.log("ident", eval("obj") === obj);  // identifier nested eval
    console.log("stmt", eval("obj.k;")); // statement-form (Phase C) nested eval
    console.log("typeof", eval("typeof obj"));
    eval("obj.n = 5;");                  // property write through nested eval
    console.log("prop-write", obj.n);
    eval("obj = 77;");                   // rebind through nested eval
    console.log("rebind", obj);
}
g();

// journal vars are function-scoped: must not leak into globals after g returns
console.log("after-g", typeof obj);
