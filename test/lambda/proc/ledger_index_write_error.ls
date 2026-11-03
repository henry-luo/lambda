// S7.1.3v3/S7.7.6v2: rejected writes return an error and skip the store's tail.
pn write_at(var xs, index, var reached) int^ {
    xs[index] = 9
    reached[0] = true
    return 17
}

pn write_typed(var xs: int[], index, var reached) float^ {
    xs[index] = 9
    reached[0] = true
    return 17.5
}

// LR12-27: no annotation or fallible signature may be needed to reject a store.
pn write_local(index, var reached) {
    var xs = [1]
    xs[index] = 9
    reached[0] = true
    return 23
}

pn write_local_block(var reached) {
    if (true) {
        var words = ["before"]
        words[2] = "after"
        reached[0] = true
    }
    reached[1] = true
}

pn main() {
    var xs = [1]
    let snapshot = xs
    var reached = [false]
    let local = write_local(5, reached) ^ { ^ }
    print([local is error, local.code, reached[0]])
    print("\n")
    let high = write_at(xs, 5, reached) ^ { ^ }
    let negative = write_at(xs, -1, reached) ^ { ^ }
    print([high is error, high.code, negative is error, negative.code,
        reached[0], xs, snapshot])
    print("\n")

    var typed: int[] = [2]
    let typed_snapshot = typed
    let typed_high = write_typed(typed, 1, reached) ^ { ^ }
    print([typed_high is error, typed_high.code, reached[0], typed, typed_snapshot])
    print("\n")

    // the failed inner store abandons its declaring block, preserving the outer tail.
    var block_reached = [false, false]
    write_local_block(block_reached)
    print(block_reached)
    print("\n")

    // a subsequent valid call still returns its native value and writes through.
    let good = write_at(xs, 0, reached)^
    let typed_good = write_typed(typed, 0, reached)^
    print([good, typed_good, reached[0], xs, snapshot, typed, typed_snapshot])
}
