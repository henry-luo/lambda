// S4.1.2/S6.1.2: ordered int lanes preserve poison and absorb either null.
pn select_lt(left: int?, right: int?) int {
    if (left < right) { return 1 }
    return 0
}

pn select_float_lt(left: float?, right: float?) int {
    if (left < right) { return 1 }
    return 0
}

pn select_read_lt(values: int[], left: int, right: int) int {
    if (values[left] < values[right]) { return 1 }
    return 0
}

pn inferred_read_lt(values, left, right) {
    if (values[left] < values[right]) { return 1 }
    return 0
}

pn compare_read(values: int[], left: int, right: int) {
    print(string(values[left] < values[right]) ++ "\n")
}

pn compare(left: int?, right: int?) {
    print(string([left < right, left <= right, left > right, left >= right,
        select_lt(left, right), select_float_lt(left, right)]) ++ "\n")
}

// D2.6.2-D2.6.3: valid poison stores retain snapshots; null still fails.
pn store_poison(var values: int[], value: int?) int {
    values[0] = value
    return 1
}

// An inferred storage witness must not turn any into an int[] annotation.
pn copy_then_compare(var values, left, right) {
    values[left] = values[right]
    return values[left] < values[1]
}

pn main() {
    var divisor = 0
    var values: int[] = [-1 div divisor, -9007199254740991, -1, 0, 1,
        9007199254740991, 1 div divisor, 0 div divisor]
    var left = 0
    while (left <= len(values)) {
        var right = 0
        while (right <= len(values)) {
            compare(values[left], values[right])
            print(string(select_read_lt(values, left, right)) ++ "\n")
            compare_read(values, left, right)
            print(string(inferred_read_lt(values, left, right)) ++ "\n")
            right = right + 1
        }
        left = left + 1
    }
    var stored: int[] = [7]
    let snapshot = stored
    store_poison(stored, 1 div divisor)
    print(string([stored[0] is inf, snapshot[0]]) ++ "\n")
    store_poison(stored, -1 div divisor)
    print(string([stored[0] == -inf, snapshot[0]]) ++ "\n")
    store_poison(stored, 0 div divisor)
    print(string([stored[0] is nan, snapshot[0]]) ++ "\n")
    var rejected = false
    store_poison(stored, null) ^ { rejected = true }
    print(string([rejected, stored[0] is nan]) ++ "\n")

    var mixed = [7, 8]
    let old_mixed = mixed
    let copied = copy_then_compare(mixed, 0, 2)
    print(string([mixed, old_mixed, copied]) ++ "\n")

}
