// D8.3.4/DF16: one invariant dynamic binder argument in a hot loop; compare
// the `_b` loop with the optional entry-guarded raw-loop sibling in release.
fn specialize(value: any as T) int => 1;
let seed = specialize(0);

pn benchmark(value: any) int {
    var total: int = 0
    var i: int = 0
    while (i < 1000000) {
        total = total + specialize(value)
        i = i + 1
    }
    return total + seed - 1
}

pn main() {
    print(benchmark(7))
}
