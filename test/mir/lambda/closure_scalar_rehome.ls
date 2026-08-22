// v3 fixture: a closure return crosses a public wrapper as a companion pair.
// The bound entry has no hidden caller-home parameter; the wrapper publishes
// lane 2 through Context::mir_companion_slot and the receiving call resolves
// it in its own active number extent. Checked by closure_scalar_rehome.mir-check
// (Stack API #24, #27).

fn make_adder(n) {
    fn inner(x) => x + n
    inner
}

let add = make_adder(i64(5000000000))

add(1)
