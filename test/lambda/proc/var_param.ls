// Phase 5: explicit var parameters document inout container mutation.

pn bump_first(var xs: any[]) {
    xs[0] = 42
}

pn main() {
    var xs: any[] = [1, 2, 3]
    bump_first(xs)
    print((xs[0]) ++ " " ++ (xs[1]) ++ "\n")
}
