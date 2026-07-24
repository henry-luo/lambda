// Call-site-informed parameter inference (Tune4 M2).
//
// A parameter with no literal evidence in its body can still be narrowed when
// every call in this unit passes the same native scalar AND the function name
// never escapes as a value. These cases pin the escape rules: whenever an
// unseen caller is possible the parameter must stay boxed, because guessing INT
// would truncate a float argument at the call boundary. Every `.25` below is a
// value that survives only if the parameter was NOT narrowed to INT.

// closed caller set, every argument an INT literal → may narrow to INT
pn div_by_sub(x, y) {
    var q = 0
    while (x >= y) {
        x = x - y
        q = q + 1
    }
    return q
}

// indirect caller: passing a function as a value hands it to callers the
// prepass cannot see
pn apply2(f, a, b) {
    return f(a, b)
}

// every DIRECT call passes INT, but the name also escapes into apply2, which
// calls it with floats → must stay boxed
pn escaped_gap(x, y) {
    return x - y
}

// mutual recursion: one entry passes INT, another FLOAT → the cycle stays boxed
pn ping(v, n) {
    if (n <= 0) { return v }
    return pong(v, n - 1)
}

pn pong(v, n) {
    if (n <= 0) { return v }
    return ping(v, n - 1)
}

// `pub` is the export surface: importers are callers this unit cannot see
pub pn public_gap(x, y) {
    return x - y
}

pn main() {
    print(string(div_by_sub(1000000, 3)) ++ "\n")
    print(string(div_by_sub(17, 5)) ++ "\n")

    print(string(escaped_gap(10, 4)) ++ "\n")
    print(string(apply2(escaped_gap, 10.5, 4.25)) ++ "\n")

    print(string(ping(7, 3)) ++ "\n")
    print(string(ping(1.25, 3)) ++ "\n")

    print(string(public_gap(9, 2)) ++ "\n")
    print(string(public_gap(9.5, 2.25)) ++ "\n")
}
