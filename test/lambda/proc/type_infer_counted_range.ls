// T19-3: `for x in a to b` lowers to a native counted loop whenever both
// bounds can be produced in the int lane. These cases pin the semantics the
// counted path must keep identical to the generic Range path (S4.8).

pn sum_to(n: int) int {
    var s: int = 0
    for i in 0 to n - 1 { s = s + i }
    return s
}

pn count_in(lo, hi) {
    var c = 0
    for i in lo to hi { c = c + 1 }
    return c
}

pn main() {
    // char ranges keep the generic Range path
    var cs = ""
    for c in "a" to "e" { cs = cs ++ c }
    print([
        sum_to(10),                        // inferred int bound from `n - 1`
        count_in(2, 5),                    // untyped params, closed int call edge
        count_in(5, 1),                    // inverted range is empty, not negative
        count_in(0, 9007199254740991 * 2), // saturated bound: rejected, zero trips
        count_in(0, null),                 // null bound: rejected, not laundered
        cs
    ])
}
