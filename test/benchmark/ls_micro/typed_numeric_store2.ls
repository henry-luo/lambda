// D3.3.3v3: narrow, floating, and wide stores with runtime element arguments.
pn update(n: int, byte: i8, small: f32, wide: u64) {
    var a: i8[] = fill(64, 0i8)
    var b: f32[] = fill(64, 0f32)
    var c: u64[] = fill(64, 0u64)
    var i = 0
    while (i < n) {
        let index = i % 64
        a[index] = byte
        b[index] = small
        c[index] = wide
        i = i + 1
    }
    return [a[63], b[63], c[63]]
}
pn main() {
    let start = clock()
    let result = update(1000000, 7i8, 0.5f32, 18446744073709551615u64)
    let elapsed = (clock() - start) * 1000.0
    print("CHECKSUM: " ++ string(result) ++ "\n")
    print("__TIMING__:" ++ elapsed ++ "\n")
}
