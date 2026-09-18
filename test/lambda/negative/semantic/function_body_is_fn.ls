// S12.1.4v2(2): a `function` body is checked as an `fn` body: it may not call
// a statically-known procedure, and procedural statements are rejected.
pn logsq(x: int) { x * x }
function calls_pn(f: function, x) => f(logsq(x))
pn main() { print(1) }
