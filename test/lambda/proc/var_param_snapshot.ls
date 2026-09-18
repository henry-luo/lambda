// LR12-10 (S9.1.2, S9.1.3): a `var` parameter is an inout borrow, but a copy
// taken from it inside the callee is a value. Writes through the parameter
// after the copy -- flat, nested, through a synthesized place handle, through a
// re-borrowing callee -- reach the caller and never the copy. Both tiers used
// to write the shared root in place, so every snapshot below saw the write.
type Cell = {k: int, n: int}
type Box = {cells: Cell?[], other: Cell, size: int}

pn mk() Box {
    return {cells: [{k: 1, n: 0}, {k: 2, n: 0}], other: {k: 5, n: 0}, size: 2}
}

// the reported shape: nested write after a whole-root copy
pn nested_write(var b: Box, i: int) int {
    var saved = b
    b.cells[i].k = 8
    return saved.cells[i].k * 1000 + b.cells[i].k
}

pn flat_write(var b: Box) int {
    var saved = b
    b.size = 8
    return saved.size * 1000 + b.size
}

pn untyped_write(var b, i: int) int {
    var saved = b
    b.cells[i].k = 8
    return saved.cells[i].k * 1000 + b.cells[i].k
}

// the root stored into a container, then written
pn stored_write(var b: Box) int {
    var held = [b]
    b.size = 7
    return held[0].size * 100 + b.size
}

// a copy returned to the caller after the write
pn returned_copy(var b: Box) Box {
    var copy = b
    b.size = 3
    return copy
}

// a reassigned alias (also wrong on T0 for local roots)
pn reassigned(var b: Box) int {
    var saved: Box = mk()
    saved = b
    b.size = 8
    return saved.size * 100 + b.size
}

pn local_reassigned() int {
    var b: Box = mk()
    var saved: Box = mk()
    saved = b
    b.size = 8
    return saved.size * 100 + b.size
}

pn array_write(var a: int[]) int {
    var s = a
    a[0] = 9
    return s[0] * 100 + a[0]
}

pn bump_first(var b: Box) {
    b.cells[0].k = b.cells[0].k + 10
}

// a re-borrowing callee writes after the copy
pn chained(var b: Box) int {
    var saved = b
    bump_first(b)
    return saved.cells[0].k * 100 + b.cells[0].k
}

// repeated spellings take a synthesized place handle (T29-1)
pn handle_write(var b: Box, i: int) int {
    var saved = b
    var before = b.cells[i].k
    b.cells[i].k = before + 7
    b.cells[i].n = 1
    return saved.cells[i].k * 1000 + b.cells[i].k * 10 + b.cells[i].n
}

// no copy: writes stay in place and reach the caller
pn unshared(var b: Box) int {
    b.size = b.size + 1
    b.cells[0].k = 3
    return b.size
}

pn main() {
    var b: Box = mk()
    print("nested=" ++ nested_write(b, 0) ++ " caller=" ++ b.cells[0].k ++ "\n")
    b = mk()
    print("flat=" ++ flat_write(b) ++ " caller=" ++ b.size ++ "\n")
    var u = mk()
    print("untyped=" ++ untyped_write(u, 1) ++ " caller=" ++ u.cells[1].k ++ "\n")
    b = mk()
    print("stored=" ++ stored_write(b) ++ " caller=" ++ b.size ++ "\n")
    b = mk()
    var r = returned_copy(b)
    print("returned=" ++ r.size ++ " caller=" ++ b.size ++ "\n")
    b = mk()
    print("reassigned=" ++ reassigned(b) ++ " caller=" ++ b.size ++ "\n")
    print("local_reassigned=" ++ local_reassigned() ++ "\n")
    var a: int[] = [1, 2]
    print("array=" ++ array_write(a) ++ " caller=" ++ a[0] ++ "\n")
    b = mk()
    print("chained=" ++ chained(b) ++ " caller=" ++ b.cells[0].k ++ "\n")
    b = mk()
    print("handle=" ++ handle_write(b, 1) ++ " caller=" ++ b.cells[1].k ++ "," ++ b.cells[1].n ++ "\n")
    b = mk()
    print("unshared=" ++ unshared(b) ++ " caller=" ++ b.size ++ "," ++ b.cells[0].k ++ "\n")
}
