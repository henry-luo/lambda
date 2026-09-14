// S7.1.3v2: an out-of-range typed read yields null, and a typed `float[]`
// store rejects null. A native float tree that carries that null as the lane
// sentinel (`dst[i] + src[i + 1]`) must reach the checked setter, not the raw
// element store, on every tier; the JIT stored the sentinel bits (read back as
// null, no rejection) since Tune26.
pn oob_var(var dst: float[], src: float[]) any^ {
    var i: int = 0
    dst[i] = dst[i] + src[i + 1]
}
pn oob_local(src: float[]) any^ {
    var dst: float[] = fill(2, 1.0)
    var i: int = 1
    dst[i] = dst[i] * src[i + 5]
    return dst
}
pn in_range(var dst: float[], src: float[]) float {
    var i: int = 0
    dst[i] = dst[i] + src[i + 1]
    return dst[0]
}
pn main() {
    var rejected = false
    var d: float[] = [1.0]
    let s: float[] = [2.0]
    oob_var(d, s) ^ { rejected = true }
    print([rejected, d[0]]); print("\n")
    var rejected2 = false
    oob_local(s) ^ { rejected2 = true }
    print(rejected2); print("\n")
    var e: float[] = [1.0]
    print(in_range(e, [0.0, 2.5])); print("\n")
}
