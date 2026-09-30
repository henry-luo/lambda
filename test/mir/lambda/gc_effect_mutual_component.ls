// D5.3.2: a closed recursive component has no collecting edge even when
// neither member can be emitted before its callee.
pn gc_mut_even(x: bool) bool {
    if (x) { return gc_mut_odd(false) }
    return true
}

pn gc_mut_odd(x: bool) bool {
    if (x) { return gc_mut_even(false) }
    return false
}

pn gc_mut_use(s: string, x: bool) int {
    if (gc_mut_even(x)) { return len(s) }
    return 0
}

pn main() { print(gc_mut_use("abc", false)); print("\n") }
