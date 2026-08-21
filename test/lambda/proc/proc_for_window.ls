// Procedural for windows follow the shipped statement semantics: the loop
// body executes over the full source because a FOR_STAM has no result stream
// for post-selection. T0 and MIR must retain that parity boundary.
pn main() {
    var seen = []
    for (x in [1, 2, 3, 4] offset 1 limit 2) {
        seen.push(x)
    }
    print(seen)
}
