// J2 (S11.4.1v3, S11.1.6v2): `int[]?` admits an int array or null, never a
// bare int. The checker only defers a scalar source here, so the runtime
// check decides it -- which the JIT skipped, binding `5`.
let v = 5
let w: int[]? = v
"bound: " ++ string(w)
