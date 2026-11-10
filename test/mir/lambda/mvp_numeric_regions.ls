// D8.2.6/S4: typed and inferred loops retain native arithmetic and edge results.
pn typed_total(n: int) int {
    var i: int = 0
    var total: int = 0
    while (i < n) { total = total + i; i = i + 1 }
    return total
}
pn erased_total(n) {
    var i = 0
    var total = 0
    while (i < n) { total = total + i; i = i + 1 }
    return total
}
pn typed_divmod(value: int) int { return value div 8 + value % 8 }
pn erased_divmod(value) { return value div 8 + value % 8 }
pn main() {
    print([typed_total(0), typed_total(12), erased_total(0), erased_total(12)]); print("\n")
    print([typed_divmod(27), erased_divmod(27)]); print("\n")
}
