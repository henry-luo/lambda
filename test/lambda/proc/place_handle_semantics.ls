// T29-1 (D4.4.4v4, CW37 Appendix D): a record place spelled repeatedly in a
// `pn` body is read and written through a synthesized handle. Every probe
// below changes the place, its container, its key or its ownership between
// two spellings; each must observe exactly what per-access navigation would.
type Cell = {k: int, n: int}
type Box = {cells: Cell?[], other: Cell, size: int}

pn fresh() Box {
    return {cells: [{k: 1, n: 0}, {k: 2, n: 0}, null], other: {k: 5, n: 0}, size: 3}
}

// write to the place through another spelling (same element at runtime)
pn alias_write(var b: Box, i: int, j: int) int {
    var before = b.cells[i].k
    b.cells[j].k = 40
    var after = b.cells[i].k
    return before * 100 + after
}

// the place itself replaced through another spelling
pn replace_place(var b: Box, i: int, j: int) int {
    var before = b.cells[i].k
    b.cells[j] = {k: 77, n: 0}
    return before * 100 + b.cells[i].k
}

// growth of the container
pn grow_prefix(var b: Box, i: int) int {
    var before = b.cells[i].k
    push(b.cells, {k: 9, n: 0})
    b.cells[i].k = before + 1
    return b.cells[i].k * 100 + len(b.cells)
}

// the root rebound
pn rebind_root(var b: Box, i: int) int {
    var before = b.cells[i].k
    b = fresh()
    return before * 100 + b.cells[i].k
}

pn bump_all(var b: Box) {
    b.cells[0].k = b.cells[0].k + 1000
}

// the root handed to a `var` callee
pn root_to_var(var b: Box, i: int) int {
    var before = b.cells[i].k
    bump_all(b)
    return before * 10000 + b.cells[i].k
}

pn peek(b: Box, i: int) int {
    return b.cells[i].k
}

// the root handed to a plain callee, then written through the place
pn root_to_plain(var b: Box, i: int) int {
    var before = b.cells[i].k
    var seen = peek(b, i)
    b.cells[i].k = before + seen
    return b.cells[i].k
}

// the key reassigned
pn key_moves(var b: Box) int {
    var i = 0
    var first = b.cells[i].k
    i = 1
    return first * 100 + b.cells[i].k
}

// a sibling slot written between spellings (kept)
pn sibling_write(var b: Box, i: int) int {
    var before = b.cells[i].k
    b.other.k = 8
    b.cells[i].k = before + b.other.k
    return b.cells[i].k
}

// a place copy taken before the handle writes: the copy is a snapshot
pn copy_then_write(var b: Box, i: int) int {
    var snap = b.cells[i]
    var before = b.cells[i].k
    b.cells[i].k = before + 50
    return snap.k * 1000 + b.cells[i].k
}

// a local root shared before the bind: the writing bind un-shares it
pn shared_root(i: int) int {
    var b: Box = fresh()
    var saved = b
    var before = b.cells[i].k
    b.cells[i].k = before + 7
    b.cells[i].n = 1
    return saved.cells[i].k * 1000 + b.cells[i].k * 10 + b.cells[i].n
}

// the element shared (it sits in a second container) before the bind
pn shared_leaf(i: int) int {
    var b: Box = fresh()
    var other: Cell?[] = [b.cells[i]]
    var before = b.cells[i].k
    b.cells[i].k = before + 30
    return other[0].k * 1000 + b.cells[i].k
}

// a local root (not a parameter), shared through a plain call
pn local_root(i: int) int {
    var b: Box = fresh()
    var seen = peek(b, i)
    var before = b.cells[i].k
    b.cells[i].k = before + seen + 100
    return b.cells[i].k
}

// a handle bound before a loop whose body writes the place through it
pn loop_kept(var b: Box, i: int) int {
    var start = b.cells[i].k
    var t = 0
    while (t < 3) {
        b.cells[i].k = b.cells[i].k + start
        t = t + 1
    }
    return b.cells[i].k
}

// a loop whose body replaces the element kills the handle before the loop
pn loop_killed(var b: Box, i: int) int {
    var start = b.cells[i].k
    var t = 0
    while (t < 2) {
        b.cells[i] = {k: b.cells[i].k + start, n: t}
        t = t + 1
    }
    return b.cells[i].k
}

// a bind inside one arm, and a kill inside the other
pn arms(var b: Box, i: int, flag: bool) int {
    var base = b.cells[i].k
    if (flag) {
        var inner = b.cells[i].n
        b.cells[i].n = inner + 3
    } else {
        b.cells[i] = {k: 0, n: 0}
    }
    return base * 100 + b.cells[i].k * 10 + b.cells[i].n
}

// an absent element: reads are null through the handle
pn absent(var b: Box, i: int) any {
    var first = b.cells[i].k
    var second = b.cells[i].n
    return [first, second]
}

pn main() {
    var b = fresh()
    print("alias=" ++ alias_write(b, 0, 0) ++ "\n")
    b = fresh()
    print("replace=" ++ replace_place(b, 1, 1) ++ "\n")
    b = fresh()
    print("grow=" ++ grow_prefix(b, 0) ++ "\n")
    b = fresh()
    print("rebind=" ++ rebind_root(b, 1) ++ "\n")
    b = fresh()
    print("root_var=" ++ root_to_var(b, 0) ++ "\n")
    b = fresh()
    print("root_plain=" ++ root_to_plain(b, 1) ++ "\n")
    b = fresh()
    print("key=" ++ key_moves(b) ++ "\n")
    b = fresh()
    print("sibling=" ++ sibling_write(b, 0) ++ "\n")
    b = fresh()
    print("copy=" ++ copy_then_write(b, 1) ++ "\n")
    print("shared=" ++ shared_root(0) ++ "\n")
    print("shared_leaf=" ++ shared_leaf(1) ++ "\n")
    print("local=" ++ local_root(1) ++ "\n")
    b = fresh()
    print("loop_kept=" ++ loop_kept(b, 1) ++ "\n")
    b = fresh()
    print("loop_killed=" ++ loop_killed(b, 0) ++ "\n")
    b = fresh()
    print("arms_t=" ++ arms(b, 0, true) ++ "\n")
    b = fresh()
    print("arms_f=" ++ arms(b, 0, false) ++ "\n")
    b = fresh()
    print("absent=" ++ absent(b, 2) ++ "," ++ absent(b, 9) ++ "\n")
    print("caller=" ++ b.cells[0].k ++ "," ++ b.cells[1].k ++ "\n")
}
