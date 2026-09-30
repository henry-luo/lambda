// S4.4.4/S11.4.1v3: a compact bitwise success fits int, but a negative shift
// must still cross the return boundary as an error.
pn compact_bitwise_int_cast(x: u32, shift_count: int) int {
    return int(bor(shl(x, shift_count), 7u32))
}

pn main() {
    var rejected = false
    let result = compact_bitwise_int_cast(3u32, 1)
    compact_bitwise_int_cast(3u32, -1) ^ { rejected = true }
    print([result, rejected])
}
