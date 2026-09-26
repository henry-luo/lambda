// LR07-19 (S12.3.2): named arguments to a statically resolved object method
// bind by name, as a direct call's do; only a dynamic callee rejects them.
// The method is called through its bound member with a positional list, so
// `t.m(b: 1, a: 5)` had bound by position on both tiers and returned 96. An
// optional parameter the names skip takes its default, or null, as on a
// direct call, and an explicit null stays null. Found with it: the static
// check compared a direct call's named arguments by position, so naming typed
// parameters out of order was rejected with E207.
// Golden written from the ruling, not from the runtime.

fn f(a, b = 42, c = 7) => [a, b, c];
fn g(a: int, b: string) => [a, b];
type T {
    k: int,
    fn m(a, b) => a - b + k,
    fn n(a, b = 42, c = 7) => [a, b, c, k],
    fn o(a, b?, c?) => [a, b, c],
    fn p(a, b: int = 42, c: string = "z") => [a, b, c]
}
let t = <T k: 100>;
"-- by name, in any order --";
[t.m(b: 1, a: 5), t.m(5, 1), t.m(5, b: 1), t.n(c: 3, b: 2, a: 1)];
"-- a skipped optional parameter takes its default, or null --";
[t.n(1, c: 3), t.n(c: 3, a: 1), t.n(b: 2, a: 1), t.n(1)];
[t.o(1, c: 3), t.p(1, c: "y"), t.p(c: "w", a: 0)];
"-- an explicit null is not an absent argument --";
[t.n(1, null, 3), f(1, null, 3)];
"-- as on a direct call --";
[f(1, c: 3), f(c: 3, a: 1), g(b: "x", a: 1), g(1, b: "y")]
