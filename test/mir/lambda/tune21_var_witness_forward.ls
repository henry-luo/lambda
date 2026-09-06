// T21-2d: a forwarding-only `var` array parameter takes the inferred array
// witness, so the callee it forwards to gets the element lane too, and a
// GUARDED store of an untyped value through that witness (the callee cannot
// prove `v` is a bool) goes through the representation-agnostic COW store
// instead of the declared-contract checked store. The latter raised E201
// against the parameter's implicit `any \ error` contract and the callee
// left through the error lane before writing, so the caller never saw the
// write (queens' set_row_column).
pn is_free(rows, r) {
    if (rows[r]) { return 1 }
    return 0
}
pn set_row(var rows, r, v) {
    rows[r] = v
}
pn place(var rows, var taken, c) {
    var r = 0
    while (r < 4) {
        if (is_free(rows, r) == 1) {
            taken[c] = r
            set_row(rows, r, false)
            if (c == 3) { return 1 }
            if (place(rows, taken, c + 1) == 1) { return 1 }
            set_row(rows, r, true)
        }
        r = r + 1
    }
    return 0
}
pn widen(var xs, i, v) {
    xs[i] = v
}
pn main() {
    var rows = fill(4, true)
    var taken = fill(4, -1)
    print(place(rows, taken, 0)); print(" "); print(taken); print(" "); print(rows); print("\n")
    var xs = fill(3, 0)
    widen(xs, 1, "x")
    print(xs); print("\n")
}
