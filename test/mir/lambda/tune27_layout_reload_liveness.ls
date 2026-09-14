// Tune27 T27-7: post-call typed-array layout reloads are pruned by liveness.
// `a` stays live across allocating calls and a loop back edge, `b` is dead
// after its last read, and every read after an allocation must see the
// compacted buffer (D5.3.4).
pn tune27_alloc(n: int) float[] { return fill(n, 1.0) }

pn tune27_layout_reload_liveness(n: int) float {
    var a: float[] = fill(8, 2.0)
    var b: float[] = fill(8, 3.0)
    var total: float = b[0] + b[7]
    var i: int = 0
    while (i < n) {
        let scratch = tune27_alloc(64)
        total = total + a[i % 8] + scratch[0]
        a[i % 8] = a[i % 8] + 1.0
        i = i + 1
    }
    let tail = tune27_alloc(16)
    return total + a[0] + tail[15]
}

pn main() {
    print(string(tune27_layout_reload_liveness(20)) ++ "\n")
}
