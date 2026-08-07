// Tune15 B2.2: a statically-bound typed-array plus scalar call carries the
// packed ArrayNum pointer and witness into the native body.

pn tune15_array_consumer(values: int[], bias: int) int {
    return values[0] + bias
}

pn main() {
    var values: int[] = fill(2, 7)
    print(tune15_array_consumer(values, 1))
    print("\n")
}
