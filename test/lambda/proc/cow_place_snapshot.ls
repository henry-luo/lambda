// CW24v2 (2026-08-29): a mutated place copy the program OBSERVES is a
// legitimate deliberate snapshot (Swift/R model) -- the bind marks the
// value, so the first write detaches and the owner is untouched. Only the
// dead-store shape (mutated, never observed) warns.
pn main() {
    var m = {rows: [{x: 1}, {x: 2}]}

    // observed snapshot: legitimate under CW24v2 -- silent
    var row = m.rows[0]
    row.x = 9
    print(row.x); print(" "); print(m.rows[0].x); print(" (ruled 9 1)\n")

    // write-back: silent, reaches m
    var r2 = m.rows[1]
    r2.x = 7
    m.rows[1] = r2
    print(m.rows[1].x); print(" (ruled 7)\n")

    // dead store: mutated, never observed -> warning only, still runs
    var dead = m.rows[0]
    dead.x = 55
}
