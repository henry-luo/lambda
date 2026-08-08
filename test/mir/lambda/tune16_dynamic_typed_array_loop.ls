// Tune16 C1: a dynamic-length typed array keeps indexed loop writes visible.
// The `j - 1` read exercises the dense native index lowering (D2.2.2).

pn tune16_dynamic_typed_array_loop() int {
    var extent: int = 7
    var values: int[] = fill(extent + 1, 0)
    var j: int = 1
    values[0] = 1
    while (j <= extent) {
        values[j] = values[j - 1] + 1
        j = j + 1
    }
    var sum: int = 0
    j = 0
    while (j <= extent) {
        sum = sum + values[j]
        j = j + 1
    }
    return values[extent] + sum
}

pn main() {
    print(tune16_dynamic_typed_array_loop())
    print("\n")
}
