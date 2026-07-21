// Procedural writes must preserve the uint64 lane and widen safely for other values.

pn print_first(values: u64[]) {
    print(values[0])
    print(",")
    print(type(values[0]))
    print("\n")
}

pn main() {
    var values: u64[] = [1u64, 2u64]
    values[0] = 18446744073709551615u64
    values[1] = 7
    print(values[0])
    print(",")
    print(values[1])
    print(",")
    print(type(values[0]))
    print("\n")

    print_first([18446744073709551615u64])

    values[1] = "wide"
    print(values[0])
    print(",")
    print(values[1])
    print(",")
    print(type(values[0]))
    print("\n")
}
