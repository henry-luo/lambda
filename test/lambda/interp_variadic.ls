// T0 variadic bindings must survive nested calls and self-tail iteration.
fn inner(...) => [len(varg()), varg(0)]

fn scoped(...) {
    let before = len(varg())
    let nested = inner(9, 8);
    [before, nested, len(varg()), varg(0)]
}

fn layout(a, b = 0, ...) => [a, b, varg()]

fn tail(n, ...) => if (n == 0) [len(varg()), varg(0)] else tail(n - 1, n)

{
    empty: scoped(),
    many: scoped(1, 2, 3),
    named: [
        layout(a: 1),
        layout(a: 1, b: 2),
        layout(1, b: 2, 3),
        layout(b: 2, a: 1, 3)
    ],
    tail: tail(3, 7, 8)
}
