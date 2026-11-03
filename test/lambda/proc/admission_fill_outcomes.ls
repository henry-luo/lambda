// S7.7.2v2/S11.4.6v3: fill success proofs do not erase deferred validation.
pn literal_fill(count) {
    var values: int[] = fill(count, 1)^
    return [len(values), values]
}

pn counted_fill(count) {
    let values: int[2] = fill(count, 1)^
    return [values is error, "continued"]
}

pn read_fill(index) {
    let source: float[] = [1.5]
    let values: float[] = fill(2, source[index])^
    return [values is error, "continued"]
}

pn literal_reassignment(count) {
    var values: int[] = fill(1, 0)^
    values = fill(count, 3)^
    return values
}

pn counted_reassignment(count) {
    var values: int[2] = [0, 0]
    values = fill(count, 3)^
    return [values is error, "continued"]
}

pn main() {
    print([literal_fill(0), literal_fill(2), literal_fill(-1) is error]); print("\n")
    print([counted_fill(1), counted_fill(2)]); print("\n")
    print([read_fill(0), read_fill(5)]); print("\n")
    print([literal_reassignment(2), literal_reassignment(-1) is error]); print("\n")
    print([counted_reassignment(1), counted_reassignment(2),
        counted_reassignment(-1) is error, counted_fill(-1) is error]); print("\n")
}
