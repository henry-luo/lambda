// an inlined activation's private scratch is reused across loop iterations (D5.3).
pn increment(values: int[], depth: int) int^ {
    values[0] = values[0] + 1
    if (depth == 0) { return values[0] }
    let child: int = increment(values, depth - 1)^
    return child + values[0]
}
pn repeat(values: int[], rounds: int) int^ {
    var i: int = 0
    var total: int = 0
    while (i < rounds) {
        total = total + increment(values, 1)^
        i = i + 1
    }
    return total
}
pn main() {
    var values: int[] = [3]
    print([repeat(values, 50000), values[0]]); print("\n")
}
