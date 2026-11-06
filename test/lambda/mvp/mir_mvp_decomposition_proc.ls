// S16.8.7/S9.1.2: a procedural RHS runs once before any target is published.
pn next(var count: int) {
    count = count + 1
    return [1e-310, 9223372036854775806i64, {n: count}]
}
pn main() {
    var count: int = 0
    var small, wide, row = next(count)
    let saved = row
    row.n = 8
    small = 2e-310
    wide = 9223372036854775805i64
    return [count == 1, small == 2e-310, wide == 9223372036854775805i64,
        saved.n == 1, row.n == 8]
}
