// D3.3.3v3/S9.1.2: exact compact lanes retain snapshots and checked misses.
pn bytes(var values: i8[], index: int, value: i8) { values[index] = value }
pn convert(var values: i8[], value: any) { values[0] = value }
pn widths() {
    var a: i8[] = [1i8, 2i8]
    var b: u8[] = [1u8, 2u8]
    var c: i16[] = [1i16, 2i16]
    var d: u16[] = [1u16, 2u16]
    var e: i32[] = [1i32, 2i32]
    var f: u32[] = [1u32, 2u32]
    var g: f16[] = [1f16, 2f16]
    var h: f32[] = [1f32, 2f32]
    var i: i64[] = [1i64, 2i64]
    var j: u64[] = [1u64, 2u64]
    let saved = a
    a[0] = -128i8
    b[0] = 255u8
    c[0] = -32768i16
    d[0] = 65535u16
    e[0] = -2147483648i32
    f[0] = 4294967295u32
    g[0] = 0.5f16
    h[0] = 1e-40f32
    i[0] = 9223372036854775807i64
    j[0] = 18446744073709551615u64
    bytes(a, 1, 7i8)
    var rejected = false
    convert(a, 300) ^ { rejected = true }
    return [a, saved, b, c, d, e, f, g, h, i, j, rejected]
}
pn main() { print(widths()) }
