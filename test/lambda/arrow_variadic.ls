// S16.9.7: an anonymous arrow takes a rest parameter as a named function
// does, and `varg()` inside it reads that arrow's own arguments --
// never an enclosing function's.
let count = (...) => len(varg())
let first_plus = (x, ...) => x + len(varg())
let base = 100
let captured = (x, ...) => base + x + sum(varg())
let second = (...) => varg(1)
fn outer(...) => ((...) => len(varg()))(7, 8)
let apply_all = (fun) => fun(1, 2, 3)

{
    counts: [count(1, 2, 3), count()],
    mixed: first_plus(10, 1, 2),
    captured: captured(1, 2, 3),
    indexed: second("a", "b"),
    own_args: outer(1, 2, 3),
    as_value: apply_all((...) => sum(varg()))
}
