// Tune30 T30-5/T30-2: a leaf `pn` that stores through a `var int[]` parameter
// is emitted at its call site, and the loop's store-ownership guard is hoisted
// across it -- the scan reads the callee's body as if it were spelled inline.

pn tune30_bump(var a: int[], i: int) any {
    a[i] = a[i] + 1
}

pn tune30_inline_store_guard(n: int) int {
    var a: int[] = fill(8, 0)
    var i: int = 0
    while (i < n) {
        tune30_bump(a, i)
        i = i + 1
    }
    return a[0] + a[7]
}

pn main() {
    print(tune30_inline_store_guard(8)); print("\n")
}
