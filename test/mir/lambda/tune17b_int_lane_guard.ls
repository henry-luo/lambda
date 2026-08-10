// Tune17b: a proven native int producer must reach the raw int[] store lane.
// Regression guard: MIR's `S` opcode suffix selects the 32-bit comparison, not
// "signed". Emitting the int53 validity check with GES/LES truncated both
// operands to int32, so the test became `(int32)v >= 1 && (int32)v <= -1` --
// unsatisfiable -- and every proven store fell through to the checked setter.

pn tune17b_swap(var v: int[], i: int, j: int) any {
    var tmp = v[i]
    v[i] = v[j]
    v[j] = tmp
}

pn main() {
    var v: int[] = [10, 20, 30, 40]
    tune17b_swap(v, 0, 1)
    print(v[0])
    print("\n")
}
