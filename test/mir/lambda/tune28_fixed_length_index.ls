// T28-7: a local bound to fill(N, v) or an N-item literal, never rebound,
// resized or handed to a callee that could resize it, has a static length.
// A literal index inside it needs no bounds test and no cold arm, and its
// element offset is a load displacement. Resizing through push drops the fact.
pn fixed_store() float {
    var values: float[] = fill(4, 0.5)
    values[3] = 2.5
    return values[0] + values[3]
}

pn fixed_literal() int {
    var flags: int[] = [0, 0, 0]
    flags[2] = 1
    return flags[2]
}

pn resized() float {
    var grown: float[] = fill(2, 0.0)
    push(grown, 4.0)
    grown[1] = 1.0
    return grown[1] + grown[2]
}

pn main() {
    print(fixed_store()); print(" ")
    print(fixed_literal()); print(" ")
    print(resized()); print("\n")
}
