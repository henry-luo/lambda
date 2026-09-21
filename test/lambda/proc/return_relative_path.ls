// `return \.a` returns the path. `\.` was missing from the tokens that start a
// return value, so it parsed as a bare `return` plus a separate `\.a` statement
// and the procedure returned null, while `return /.a` worked (S2.4.1v2).
pn relative() {
    return \.a.b
}
pn rooted() {
    return /.a.b
}
pn bare() {
    return \
}
pn main() {
    print([relative(), rooted(), bare()])
}
