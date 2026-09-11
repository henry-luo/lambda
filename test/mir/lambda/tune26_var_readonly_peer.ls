// Tune26 T26-2/T26-4: a var destination retains its COW-safe checked path,
// while a separate readonly float[] parameter may consume the dense read proof.

pn tune26_var_readonly_peer(var dst: float[], src: float[], n: int) any {
    var i: int = 0
    while (i < n) {
        dst[i] = src[i] * 2.0
        i = i + 1
    }
}

pn main() {
    var dst: float[] = fill(4, 1.0)
    let snapshot = dst
    let src: float[] = [1.0, 2.0, 3.0, 4.0]
    tune26_var_readonly_peer(dst, src, 4)
    print(dst[2]); print(" ")
    print(snapshot[2]); print(" ")
    print(src[2]); print("\n")
}
