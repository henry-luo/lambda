// S4.1.2/S4.1.5: bounded expansion retains poison and checked sized locals.
pn recursive_sum(n: int) int {
    if (n <= 0) { return 0 }
    return n + recursive_sum(n - 1)
}
pn twice(x: int) int { return x + x }
pn nested(x: int) int { return twice(twice(x)) }
pn wrapping_add(x: int, y: int) {
    var a: u32 = x
    var b: u32 = y
    return int(a + b)
}
pn rotate(x: int, shift: int) {
    var value: u32 = x
    return int(bor(shl(value, shift), shr(value, 32 - shift)))
}
pn tail_div(x: int, y: int, q: int) int {
    if (x < y) { return q }
    return tail_div(x - y, y, q + 1)
}
pn outer_div(x: int, y: int) int { return tail_div(x, y, 0) }
pn counted_scan() int {
    var total: int = 0
    var i: int = 0
    while (i < 4) { total = total + outer_div(12, 2); i = i + 1 }
    return total
}
pn main() {
    print([recursive_sum(12), nested(7), nested(-7)]); print("\n")
    print([wrapping_add(4294967295, 1), wrapping_add(2147483648, 2147483648),
        type(wrapping_add(-1, 1)) == error]); print("\n")
    print([rotate(1, 1), rotate(1, 31), rotate(1, 32),
        type(rotate(1, -1)) == error, type(rotate(-1, 1)) == error]); print("\n")
    print([nested(9007199254740991), nested(-9007199254740991)]); print("\n")
    print(counted_scan()); print("\n")
}
