// Tune4 M2 fixture: a closed, uniformly typed pure function may narrow its
// parameters, while a function that escapes as a value retains generic
// arithmetic for unseen float callers.

fn closed_gap(x, y) { x - y }

fn open_gap(x, y) { x - y }

pn apply2(f, a, b) {
    return f(a, b)
}

pn main() {
    print(string(closed_gap(17, 5)) ++ "\n")
    print(string(open_gap(10, 4)) ++ "\n")
    print(string(apply2(open_gap, 10.5, 4.25)) ++ "\n")
}
