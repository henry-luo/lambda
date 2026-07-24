// Tune4 M1 fixture: declared numeric arrays need a runtime representation
// guard before inline loads/stores because another alias can still widen them.

pn guarded_load(a: float[], i: int) {
    return a[i]
}

pn guarded_store(a: float[], i: int) {
    a[i] = 9.5
    return a[i]
}

pn main() {
    var values = fill(4, 1.25)
    print(string(guarded_load(values, 2)) ++ "\n")
    print(string(guarded_store(values, 1)) ++ "\n")
}
