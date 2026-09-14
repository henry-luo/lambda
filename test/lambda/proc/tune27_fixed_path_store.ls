// T27-4: nested writes through a declared root use the fixed-key checked
// setter (one to three keys). Pins its results against the transactional path:
// open-array leaves, declared leaves, bracket keys, detach of a plain root,
// rejected values and the four-key array-descriptor fallback.

type Rec = {n: int, flag: bool, tag: string}
type Box = {count: int, items: array, rec: Rec, grid: array}

pn bump(var b: Box, i: int) {
    b.items[i].n = b.items[i].n + 1
    b.items[i].flag = true
    b.rec.n = b.rec.n + 10
}

pn set_any(var b: Box, i, v) {
    b.items[i].n = v
}

pn set_deep(var b: Box, i: int, j: int, v: int) {
    b.grid[i][j].n = v
}

pn store_bad(var b: Box, v) {
    b.rec.n = v
}

pn run_hot(var b: Box) {
    var k = 0
    while (k < 400) {
        bump(b, k % 3)
        k = k + 1
    }
}

pn main() {
    var b: Box = {count: 0, items: [{n: 1, flag: false, tag: "a"},
                                    {n: 2, flag: false, tag: "b"},
                                    {n: 3, flag: false, tag: "c"}],
                  rec: {n: 0, flag: false, tag: "r"},
                  grid: [[{n: 0}, {n: 0}], [{n: 0}, {n: 0}]]}
    run_hot(b)
    print([b.items[0].n, b.items[1].n, b.items[2].n, b.items[0].flag, b.rec.n])
    print("\n")

    // an open leaf accepts any non-error value; the bracket key is an Item
    var idx = 1
    set_any(b, idx, "text")
    set_any(b, 2.0, 7)
    print([b.items[1].n, b.items[2].n])
    print("\n")

    // four keys take the array descriptor
    set_deep(b, 1, 0, 42)
    print(b.grid[1][0].n)
    print("\n")

    // a declared leaf still rejects a wrong value without writing
    let r = store_bad(b, "not an int")^ { print("rejected ") }
    print(b.rec.n)
    print("\n")

    // a plain local root is detached, so a snapshot keeps its old value
    var c: Box = b
    c.items[0].n = -5
    print([b.items[0].n, c.items[0].n])
    print("\n")
}
