// S12.1.4v2: in `fn` context a statically-known `pn` argument in a polymorphic
// slot makes the call a `pn` call, which is a compile error.
pn logsq(x: int) { x * x }
function apply_all(f: function, xs) => for (x in xs) f(x)
fn bad(xs) => apply_all(logsq, xs)
pn main() { print(bad([1])) }
