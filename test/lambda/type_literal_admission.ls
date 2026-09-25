// S11.2.1 (LR03-11): literal types are singletons, matched by `==` as literal
// match arms are. Membership and admission test the value, not its carrier.
fn dyn(v) => v
type T = 1 | 2
fn g(x: 1 | 2) { x }
fn one(x: 1) { x }
fn pick(x: "a" | "b") { x }
let member = [3 is T, 2 is T, 1.0 is 1, "c" is ("a" | "b"), "a" is ("a" | "b")]
let admitted = [g(dyn(2)), one(dyn(1.0)), type(one(dyn(1.0))), pick(dyn("b"))]
let r = [member, admitted]
r
