// Stress boxed-float liveness across GC. Boxed float locals should be tracked as
// ANY roots; FLOAT locals should stay native MIR_T_D.

fn make_float(x) {
    x + 0.25
}

pn churn(n: int) {
    var total = 0
    var i = 0
    while (i < n) {
        let a = fill(1000, i)
        total = total + sum(a)
        i = i + 1
    }
    total
}

pn main() {
    var boxed_any = make_float(41.75)
    var before = boxed_any
    var noise = churn(8000)
    var after = boxed_any + 0.0
    print([before, noise > 0, after, after == 42.0])
    print("\n")
}
