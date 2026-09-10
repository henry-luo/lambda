// D8.2.6/D8.4.3v2: invariant helper calls and discarded loop output.
pn accumulate(n: int, value: float) float {
    let scale = value * 2.0
    var total = 0.0
    for (i in 1 to n) { total = total + abs(scale) }
    return total
}
pn main() {
    let start = clock()
    let total = accumulate(2000000, -2.5)
    let elapsed = (clock() - start) * 1000.0
    print("CHECKSUM: " ++ total ++ "\n")
    print("__TIMING__:" ++ elapsed ++ "\n")
}
