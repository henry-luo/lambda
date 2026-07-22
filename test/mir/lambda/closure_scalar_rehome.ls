// MT5 fixture: a scalar leaving an activation through a boundary with no
// declared owner is copied into GC-owned storage rather than keeping a pointer
// into the activation. The bound (public) entry wrapper has no caller-donated
// home, so it allocates its own, calls the direct body, and rehomes the result
// before restoring its watermarks.
// Checked by closure_scalar_rehome.mir-check (Stack API #24, #27).

fn make_adder(n) {
    fn inner(x) => x + n
    inner
}

let add = make_adder(int64(5000000000))

add(1)
