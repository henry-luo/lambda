// Tune26 T26-4: a unique var bool[] parameter can consume its checked
// loop-entry ownership proof while the checked setter remains available.

pn tune26_var_param_unique(var flags: bool[], n: int) int {
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
    var flags: bool[] = fill(8, true)
    print(tune26_var_param_unique(flags, 8)); print("\n")
}
