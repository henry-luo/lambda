// D8.1.1v8: a body that REBINDS a typed `var` parameter stays in T0 (the raw
// satellite has no home to publish through), and its callers -- T0 or a
// promoted satellite -- observe the rebind through the transported home.
// The eager JIT (LAMBDA_TIER=jit) loses this rebind today: its direct native
// edge passes the raw container and never reloads (CW33's typed `Container**`
// half is not implemented there); the golden is T0's, which the auto tier
// now matches.
pn rebind(var x: float[], v: float) { x = fill(2, v) }
pn caller(var xs: float[], n: int) {
    var i = 0
    while (i < n) { rebind(xs, float(i)); i = i + 1 }
}
pn main() {
    var rs: float[] = fill(4, 0.0)
    var n = 0
    while (n < 6) { rebind(rs, float(n)); n = n + 1 }
    print(rs, "\n")
    var ts: float[] = fill(3, 1.0)
    caller(ts, 7)
    print(ts, "\n")
}
