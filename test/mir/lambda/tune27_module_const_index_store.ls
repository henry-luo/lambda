// Tune27 T27-3: an immutable module int inside a store subscript is a native
// index leaf, so `x[j * ROW] = ...` through a `var float[]` parameter keeps the
// machine index and lane setter instead of boxing its key into the Item setter.
// The caller must still observe every in-place write (S9.2.2).
let WIDE = 6
let ROW = WIDE + 2

pn tune27_module_const_index_store(var x: float[]) any {
    var j: int = 1
    while (j <= 3) {
        x[j * ROW] = 0.0 - x[1 + j * ROW]
        x[(WIDE + 1) + j * ROW] = x[WIDE + j * ROW] * 2.0
        j = j + 1
    }
}

pn main() {
    var grid: float[] = fill(ROW * 5, 0.0)
    var k: int = 0
    while (k < ROW * 5) {
        grid[k] = float(k)
        k = k + 1
    }
    tune27_module_const_index_store(grid)
    print([grid[8], grid[15], grid[16], grid[23], grid[24], grid[31]])
    print("\n")
}
