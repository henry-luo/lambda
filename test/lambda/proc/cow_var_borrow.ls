// S9.1.3/S9.2.2: `var` parameters are the SOLE sharing construct -- an
// exclusive inout borrow. Writes through the borrow reach the caller; the
// borrow chain stays single-writer by construction (re-borrow, recursion).
// CW25: a PLACE argument (`f(var m.rows[i])`) borrows through the spine.

pn bump(var m) { m.n = m.n + 1 }

pn bump_twice(var m) {
    bump(m)          // re-borrow: single writer chain, no check needed
    bump(m)
}

pn countdown(var acc, k: int) {
    if (k <= 0) { return null }
    acc.n = acc.n + k
    countdown(acc, k - 1)   // recursive re-borrow
}

pn set_row(var row) { row.mark = "written" }

pn fill_slot(var xs) { xs[1] = "B" }

pn main() {
    // basic write-through
    var a = {n: 10}
    bump(a)
    print(a.n); print(" (borrow: ruled 11)\n")

    // re-borrow chain
    var b = {n: 0}
    bump_twice(b)
    print(b.n); print(" (re-borrow: ruled 2)\n")

    // recursion through the borrow
    var c = {n: 0}
    countdown(c, 4)
    print(c.n); print(" (recursion: ruled 10)\n")

    // CW25 path borrow: the PLACE is borrowed, the spine updated in place
    var m = {rows: [{mark: "clean"}, {mark: "clean"}]}
    set_row(m.rows[1])
    print(m.rows[0].mark); print(" "); print(m.rows[1].mark);
    print(" (path borrow: ruled clean written)\n")

    // array borrow next to a VALUE argument: the value arg snapshots BEFORE
    // the borrow's mutation begins (C4.2c ordering)
    var xs: any[] = ["A", "old", "C"]
    var snapshot = xs
    fill_slot(xs)
    print(xs[1]); print(" "); print(snapshot[1]);
    print(" (ordering: ruled B old)\n")
}
