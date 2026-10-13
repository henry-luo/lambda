// Tail position belongs only to a form whose value IS one child's value — a
// call, parentheses, the arms of if/match, a block's lone value, a return.
// A constructor or operator around a self-call must get the call's value
// back: `{*:m1(n - 1), type: "top"}` wraps it. The JIT let the tail-position
// flag leak into these operands, so with TCO on the inner call became the
// loop jump and the function returned the callee's value, dropping the
// wrapper (a map spread lost its override; `[*xs, 8]` lost its 8; `-f(x)`
// lost its sign). The interpreter marks tail calls per form and never did.

fn m1(n) => if (n <= 0) {type: "leaf"} else if (n == 1) {*:m1(n - 1), type: "top"} else m1(n - 1)
fn m2(n) => if (n <= 0) 7 else if (n == 1) {k: m1(0), v: m2(n - 1)} else m2(n - 1)
fn a1(n) => if (n <= 0) 7 else if (n == 1) [m2(0), a1(n - 1)] else a1(n - 1)
fn a2(n) => if (n <= 0) [7] else if (n == 1) [*a2(n - 1), 8] else a2(n - 1)
fn e1(n) => if (n <= 0) 7 else if (n == 1) <e a1(n - 1)> else e1(n - 1)
fn e2(n) => if (n <= 0) 7 else if (n == 1) <e k: e2(n - 1)> else e2(n - 1)
fn l1(n) => if (n <= 0) 7 else if (n == 1) (l1(n - 1), 8) else l1(n - 1)
fn l2(n) => if (n <= 0) 7 else if (n == 1) (8, l2(n - 1)) else l2(n - 1)
fn b1(n) => if (n <= 0) 7 else if (n == 1) { 8; b1(n - 1) } else b1(n - 1)
fn u1(n) => if (n <= 0) 7 else if (n == 1) -u1(n - 1) else u1(n - 1)
fn x1(n) => if (n <= 0) [7, 9] else if (n == 1) x1(n - 1)[1] else x1(n - 1)
fn f1(n) => if (n <= 0) {t: 7} else if (n == 1) f1(n - 1).t else f1(n - 1)
fn s1(n) => if (n <= 0) "a" else if (n == 1) "<" ++ s1(n - 1) else s1(n - 1)
fn h1(n) any^ => if (n <= 0) raise error("x") else if (n == 1) h1(n - 1) ^ { 42 } else h1(n - 1)
// a let-list's lone value is still a tail position, so this one does loop
// (typed: the JIT's tail-argument proof does not yet cover an untyped
// let-list local, so an untyped `t1` recurses on that tier)
fn t1(n: int, acc: int) int => if (n <= 0) acc else (let next = acc + n, t1(n - 1, next));

[m1(2), m2(2), a1(2), a2(2), e1(2), e2(2), l1(2), l2(2), b1(2), u1(2), x1(2), f1(2), s1(2)];
[h1(2) ^ { -1 }, t1(100000, 0)]
