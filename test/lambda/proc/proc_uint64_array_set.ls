// Typed writes preserve the uint64 lane; untyped values may evolve separately.

pn print_first(values: u64[]) {
    print(values[0])
    print(",")
    print(type(values[0]))
    print("\n")
}

pn main() {
    var values: u64[] = [1u64, 2u64]
    values[0] = 18446744073709551615u64
    values[1] = 7u64
    print(values[0])
    print(",")
    print(values[1])
    print(",")
    print(type(values[0]))
    print("\n")

    print_first([18446744073709551615u64])

    var widened = [values[0], "wide"]
    print(widened[0])
    print(",")
    print(widened[1])
    print(",")
    print(type(widened[0]))
    print("\n")
}
