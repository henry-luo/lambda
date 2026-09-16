// Tune28 T28-1 / RC3.3 / D2.4.1: a module-scope `let` bound to a literal is a
// compile-time constant. Its reads must be immediates, never a boxed module
// slab load followed by a tag dispatch and an unbox call. The signed form
// (`-1`) settles in the same binding-copy arm, so it needs no heap either and
// folds on the eager path where the fold pass used to decline outright.
// A function-local `let` is deliberately NOT folded: it is already a live
// register, and trading that reuse for a fresh immediate per use only grows
// the body (cube3d's `run_cube` regressed exactly that way).

let STEP = 3
let BACK = -1
let SCALE = 2.5
let ON = true

pn scan(limit: int) float {
    var i: int = 0
    var hits: int = 0
    while (i < limit) {
        if (i == STEP and ON) { hits = hits + 1 }
        if (i == BACK) { hits = hits + 2 }
        i = i + 1
    }
    return float(hits) * SCALE
}

pn main() {
    print(scan(40))
}
