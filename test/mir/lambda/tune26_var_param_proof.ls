// Tune26 T26-4: a var bool[] parameter keeps its admitted contract and cached
// layout, while every direct store still carries the caller-visible COW guard.

pn tune26_var_param_proof(var flags: bool[], n: int) int {
    var count: int = 0
    var i: int = 0
    while (i < n) {
        flags[i] = false
        if (not flags[i]) { count = count + 1 }
        i = i + 1
    }
    return count
}

pn main() {
    var flags: bool[] = fill(6, true)
    let snapshot = flags
    print(tune26_var_param_proof(flags, 6)); print(" ")
    print(snapshot[0]); print(" ")
    print(flags[0]); print("\n")
}
