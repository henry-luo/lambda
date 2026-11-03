// S9.1.3/S9.1.4v2: shared parent homes preserve COW and wide scalar copies.
fn discard(value) => 0
fn owner(seed) {
    var scalar = 1.000000000000001
    var wide: i64 = 9223372036854775806i64
    var rows = [{value: seed}]
    pn modify() { rows[0].value = 99; scalar = 3.000000000000001; wide = 1 }
    discard([modify])
    let old_scalar = scalar
    let old_wide = wide
    let saved = () => [rows, scalar, wide]
    rows[0].value = seed + 1
    scalar = 2.000000000000001
    wide = 9223372036854775805i64
    let current = [rows, scalar, wide]
    let snapshot = saved()
    return [old_scalar, old_wide, snapshot, current]
}
[owner(10), owner(20)]
