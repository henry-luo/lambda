// S7.1.1v3 + S11.4.1v3 + S7.7.4: `math.sqrt` of an out-of-range `float[]` read
// is null, so the sum is null, and reassigning it to a declared `float` raises
// E201 naming the binding. The JIT's native libm call had turned the null into
// NaN, which the accumulator kept silently.
pn main() {
    let v: float[] = [1.0]
    var total: float = 1.5
    total = total + math.sqrt(v[4])
    print(["bound: ", total])
}
