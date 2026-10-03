// Variadic definitions promoted out of T0 under default AUTO thresholds:
// a satellite entered with no rest arguments must see an empty `varg()`
// (the dynamic-call adapter's no-rest sentinel is not a list), and a
// self-tail handoff must carry the activation's rest arguments.
fn probe(...) => [len(varg()), varg(0)]

fn count_down(n, ...) => if (n == 0) [len(varg()), varg(0)] else count_down(n - 1, n)

{
    empty: [for (i in 1 to 8) probe()],
    some: [for (i in 1 to 8) probe(i, 0)],
    tail: count_down(40, 7, 8)
}
