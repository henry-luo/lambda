// T22-2a: a nullable typed-array lane uses raw equality only after an in-band
// guard. The slow arm must retain floating equality for a missing value.
// T28-7: against a proven in-band operand (a literal) the raw compare is
// already exact, so only the two-read form keeps the floating arm.
pn classify_tune22(values: int[], index: int, other: int) int {
    if (values[index] == values[other]) { return 1 }
    if (values[index] != values[other]) { return 2 }
    return 3
}

pn classify_literal(values: int[], index: int) int {
    if (values[index] == 2) { return 1 }
    if (values[index] != 2) { return 2 }
    return 3
}

pn main() {
    var values: int[] = fill(2, 0)
    values[1] = 2
    print(classify_tune22(values, 1, 1)); print(" ")
    print(classify_tune22(values, 8, 9)); print(" ")
    print(classify_literal(values, 1)); print(" ")
    print(classify_literal(values, 8)); print("\n")
}
