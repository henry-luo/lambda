// D8.3.2: proved scalar tail edges loop; open keys keep source recursion.
pn tune32_count(n, acc) {
    if (n <= 0) { return acc }
    return tune32_count(n - 1, acc + 1)
}
pn tune32_float_count(n, acc) {
    if (n <= 0) { return acc }
    return tune32_float_count(n - 1, acc + 0.25)
}
pn tune32_permute(n, left, right) {
    if (n <= 0) { return left * 10 + right }
    if (n > 2) { return tune32_permute(n - 1, right, left) }
    return tune32_permute(n - 1, right, left)
}
pn tune32_shape_change(n, acc) {
    if (n <= 0) { return acc }
    if (n == 1) { return tune32_shape_change(0, "done") }
    return tune32_shape_change(n - 1, acc + 1)
}
pn tune32_non_tail(n) {
    if (n <= 0) { return 1 }
    return tune32_non_tail(n - 1) + 1
}
pn tune32_alloc_int(n: int) int {
    let allocated = fill(3, n)
    return n
}
pn tune32_alloc_tail(n, acc) {
    if (n <= 0) { return acc }
    return tune32_alloc_tail(tune32_alloc_int(n - 1), acc + 1)
}
pn main() {
    print(tune32_count(256, 0)); print(" ")
    print(tune32_float_count(8, 1.5)); print(" ")
    print(tune32_permute(5, 2, 7)); print("\n")
    let escaped = tune32_count
    print(escaped(3, 1.5)); print(" "); print(escaped(0, null)); print("\n")
    print(tune32_shape_change(3, 0)); print(" ")
    print(tune32_non_tail(5)); print(" ")
    print(tune32_alloc_tail(9, 0)); print("\n")
}
