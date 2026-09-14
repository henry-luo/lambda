fn make_i64(value: i64) i64?[] { [value] }
fn make_u64(value: u64) u64?[] { [value] }

pn main() {
    let first_i64 = make_i64(9007199254740995i64)
    let second_i64 = make_i64(43i64)
    let first_u64 = make_u64(18446744073709551615u64)
    let second_u64 = make_u64(44u64)

    var source_i64: i64[] = [7i64]
    var nullable_copy: i64?[] = source_i64
    nullable_copy[0] = null

    print(string([first_i64[0], second_i64[0], first_u64[0], second_u64[0],
        source_i64[0], nullable_copy[0]]) ++ "\n")
}
